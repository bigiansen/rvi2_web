#include "cozy_widget.hpp"

#include <fstream>
#include <sstream>
#include <Wt/WCssDecorationStyle.h>
#include <Wt/WJavaScript.h>

const std::string cozy_widget::PX_SHADER_PATH = "shaders/pxshader.glsl";
const std::string cozy_widget::VX_SHADER_PATH = "shaders/vxshader.glsl";

#define RESIZE_JS() doJavaScript( \
                        " Wt.emit('" + id() + "', 'resize_signal'" + \
                        ", window.innerWidth" + \
                        ", window.innerHeight)")

void cozy_widget::init_resize_timer(int pollrate_ms)
{
    //std::stringstream jstr;
    //jstr << "setInterval("
    //     << "function() {"
    //     << "Wt.emit('" + id() + "', 'resize_signal', window.innerWidth, window.innerHeight)}"
    //     << ", " + std::to_string(pollrate_ms) + ")";
    //doJavaScript(jstr.str());
}

void cozy_widget::resize_signal(int w, int h) 
{
    float win_aspect_ratio = static_cast<float>(h) / static_cast<float>(w);
    float vpt_aspect_ratio = _aspect_ratio_y / _aspect_ratio_x;

    int final_w, final_h;
    int w_rem, h_rem;

    if(vpt_aspect_ratio > win_aspect_ratio)
    {
        final_h = h;
        final_w = static_cast<int>(h * vpt_aspect_ratio);

        h_rem = 0;
        w_rem = w - final_w;
    }
    else
    {
        final_w = w;
        final_h = static_cast<int>(w * vpt_aspect_ratio);

        w_rem = 0;
        h_rem = h - final_h;
    }

    resizeGL(final_w, final_h);
    resize(final_w, final_h);

    recenter_widget(w_rem / 2, h_rem / 2);
}

void cozy_widget::recenter_widget(int x_pos, int y_pos)
{
    setPositionScheme(Wt::PositionScheme::Absolute);
    setOffsets(x_pos, Wt::Side::Left);
    setOffsets(y_pos, Wt::Side::Top);
}

void cozy_widget::init_lua_methods()
{
    sol::state* lua_state = get_client_instance()->get_lua_context()->get_lua_state();

    lua_state->set_function("set_viewport_aspect_ratio", [&](float x, float y)
    {
        _aspect_ratio_x = x;
        _aspect_ratio_y = y;
    });

    lua_state->set_function("wgl_lineWidth", [&](int width)
    {
        lineWidth(width);
    });
}

cozy_widget::cozy_widget()
    : _resize_signal(this, "resize_signal")
{
    setId("_COZY_WIDGET_");
    run_js("init.js");

    setStyleClass("nomp");
    init_resize_timer(1000);
    // Disable padding
    doJavaScript("document.getElementById(document.getElementById('"+id()+"').parentElement.id).style.padding=0");

    _resize_signal.connect(this, &cozy_widget::resize_signal);
    setLayoutSizeAware(true);

    //Wt::WLength len(100, Wt::LengthUnit::Percentage);
    //resize(len, len);
    
    decorationStyle().setBackgroundColor(Wt::WColor(100, 100, 100));
    setMargin(0);
    
    _client_id = _runtime.create_client();
}

void cozy_widget::init()
{
    _runtime.start_client(_client_id);

    this->clicked().connect([&](const Wt::WMouseEvent& e)
    {
        auto inst = _runtime.get_instance(_client_id);
        Wt::Coordinates coords = e.widget();
        float x = 
            static_cast<float>(coords.x) / static_cast<float>(this->width().value());
        float y = 
            1.0F - (static_cast<float>(coords.y) / static_cast<float>(this->height().value()));
        inst->user_click(rvi::vector2(x, y));
        refresh_snapshot();
        repaintGL(Wt::GLClientSideRenderer::RESIZE_GL);
        repaintGL(Wt::GLClientSideRenderer::PAINT_GL);
        RESIZE_JS();
    });

    RESIZE_JS();
}

void cozy_widget::compile_shaders()
{
    // Read shaders
    std::string vx_sh_src, px_sh_src;

    std::ifstream vx_fstream(VX_SHADER_PATH);
    {
        vx_fstream >> std::noskipws;
        std::stringstream vx_ss;
        vx_ss << vx_fstream.rdbuf();
        vx_sh_src = vx_ss.str();
    } vx_fstream.close();

    std::ifstream px_fstream(PX_SHADER_PATH);
    {
        px_fstream >> std::noskipws;
        std::stringstream px_ss;
        px_ss << px_fstream.rdbuf();
        px_sh_src = px_ss.str();
    } px_fstream.close();

    // Compile and link shaders
    _sh_program = createProgram();
    auto vxsh = createShader(VERTEX_SHADER);
    auto pxsh = createShader(FRAGMENT_SHADER);

    shaderSource(vxsh, vx_sh_src);
    shaderSource(pxsh, px_sh_src);    

    compileShader(vxsh);
    compileShader(pxsh);

    attachShader(_sh_program, vxsh);
    attachShader(_sh_program, pxsh);

    linkProgram(_sh_program);

    validateProgram(_sh_program);

    deleteShader(vxsh);
    deleteShader(pxsh);

    useProgram(_sh_program);
}

void cozy_widget::refresh_snapshot()
{
    if(!_gl_initialized)
    {
        return;
    }

    _vx_data.clear();
    rvi::relative_snapshot snapsh = _runtime.snapshot_full_relative(_client_id);
    for(auto& entry : snapsh)
    {
        entry.lines.move_into(_vx_data);
    }

    if(_vx_data.empty())
    {
        return;
    }

    bindBuffer(ARRAY_BUFFER, _vbo_pos);
    bufferDatafv(
        ARRAY_BUFFER, 
        _vx_data.position_cbegin(), 
        _vx_data.position_cend(), 
        DYNAMIC_DRAW,
        true
    );

    AttribLocation loc_vx_pos = getAttribLocation(_sh_program, "vertex_position");
    vertexAttribPointer(loc_vx_pos, 2, FLOAT, false, 8, 0);
    enableVertexAttribArray(loc_vx_pos);

    bindBuffer(ARRAY_BUFFER, _vbo_col);
    bufferDataiv(
        ARRAY_BUFFER,
        _vx_data.color_cbegin(), 
        _vx_data.color_cend(), 
        DYNAMIC_DRAW,
        UNSIGNED_INT
    );
    
    AttribLocation loc_vx_col = getAttribLocation(_sh_program, "vertex_color");
    vertexAttribPointer(loc_vx_col, 4, BYTE, false, 4, 0);
    enableVertexAttribArray(loc_vx_col);
}

void cozy_widget::initializeGL()
{
    compile_shaders();
    _vbo_pos = createBuffer();
    _vbo_col = createBuffer();
    _gl_initialized = true;
    refresh_snapshot();
}

static int www,hhh;

void cozy_widget::resizeGL(int width, int height)
{
    this->viewport(0, 0, width, height);
}

void cozy_widget::paintGL()
{
    refresh_snapshot();
    clear(COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT);
    clearColor(0.2, 0.2, 0.2, 0.5);
    useProgram(_sh_program);
    if(!_vx_data.empty())
    {
        drawArrays(LINES, 0, _vx_data.size() * 2);
    }    
}

void cozy_widget::run_js(const std::string& filename)
{
    std::fstream fs("js/" + filename);
    if(fs)
    {
        std::stringstream sstr;
        sstr << fs.rdbuf();
        doJavaScript(sstr.str());
    }
    else
    {
        std::cerr << "[cozy_widget::run_js] File:" << filename << " not found!";
    }
}