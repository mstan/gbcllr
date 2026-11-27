#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SDL_opengl.h>
#include <cstring>
#include <cstdio>

// SDL's desktop OpenGL renderer owns the context. Resolve its entry points
// locally so systems without desktop GL still build and use the CPU fallback.
class OpenGlLcd {
    SDL_Renderer* renderer_;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture_{nullptr,SDL_DestroyTexture};
    std::vector<uint32_t> previous_;
    unsigned width_=0,height_=0;
    GLuint program_=0;
    GLint size_uniform_=-1,texture_scale_uniform_=-1;
    bool rectangle_textures_=false;
    bool tried_=false;
    bool failed_=false;
    SDL_GLContext context_=nullptr;
    uint64_t count_=0,bytes_=0;
#define LCD_GL(ret,name,args) using name##_type=ret (APIENTRY*) args;name##_type name=nullptr;
    LCD_GL(GLuint,CreateShader,(GLenum)) LCD_GL(void,ShaderSource,(GLuint,GLsizei,const GLchar* const*,const GLint*))
    LCD_GL(void,CompileShader,(GLuint)) LCD_GL(void,GetShaderiv,(GLuint,GLenum,GLint*)) LCD_GL(void,DeleteShader,(GLuint))
    LCD_GL(GLuint,CreateProgram,()) LCD_GL(void,AttachShader,(GLuint,GLuint)) LCD_GL(void,LinkProgram,(GLuint))
    LCD_GL(void,GetProgramiv,(GLuint,GLenum,GLint*)) LCD_GL(void,DeleteProgram,(GLuint)) LCD_GL(void,UseProgram,(GLuint))
    LCD_GL(GLint,GetUniformLocation,(GLuint,const GLchar*)) LCD_GL(void,Uniform1i,(GLint,GLint))
    LCD_GL(void,Uniform2f,(GLint,GLfloat,GLfloat)) LCD_GL(void,Uniform4f,(GLint,GLfloat,GLfloat,GLfloat,GLfloat))
    LCD_GL(void,GetIntegerv,(GLenum,GLint*)) LCD_GL(void,GetBooleanv,(GLenum,GLboolean*)) LCD_GL(void,GetFloatv,(GLenum,GLfloat*))
    LCD_GL(GLboolean,IsEnabled,(GLenum)) LCD_GL(void,Enable,(GLenum)) LCD_GL(void,BindTexture,(GLenum,GLuint))
    LCD_GL(void,ActiveTexture,(GLenum)) LCD_GL(void,Disable,(GLenum)) LCD_GL(void,Viewport,(GLint,GLint,GLsizei,GLsizei))
    LCD_GL(void,ColorMask,(GLboolean,GLboolean,GLboolean,GLboolean)) LCD_GL(void,PolygonMode,(GLenum,GLenum))
    LCD_GL(void,Begin,(GLenum)) LCD_GL(void,End,()) LCD_GL(void,TexCoord2f,(GLfloat,GLfloat)) LCD_GL(void,TexCoord4fv,(const GLfloat*)) LCD_GL(void,Vertex2f,(GLfloat,GLfloat))
#undef LCD_GL
    // The ARGB SDL bridge binds/enables one texture on unit zero. The LCD
    // changes only the states below, not lighting, matrices, pixel transfer,
    // stencil/depth parameters or the other texture units. Keep SDL's own
    // bind/unbind calls so its renderer cache is invalidated as documented.
    struct DrawState {
        OpenGlLcd& gl;
        static constexpr GLenum caps[]={GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,GL_CULL_FACE,GL_ALPHA_TEST};
        static constexpr GLenum rectangle=0x84f5,rectangle_binding=0x84f6;
        GLint program=0,active=0,texture=0,rectangle_texture=0,viewport[4]{},polygon[2]{};
        GLboolean enabled[6]{},texture_enabled=0,rectangle_enabled=0,color[4]{};
        GLfloat coords[4]{};
        bool bound=false,color_changed=false,polygon_changed=false,viewport_changed=false;
        explicit DrawState(OpenGlLcd& owner):gl(owner){
            gl.GetIntegerv(GL_CURRENT_PROGRAM,&program);gl.GetIntegerv(GL_ACTIVE_TEXTURE,&active);
            if(active!=GL_TEXTURE0)gl.ActiveTexture(GL_TEXTURE0);
            gl.GetIntegerv(GL_TEXTURE_BINDING_2D,&texture);texture_enabled=gl.IsEnabled(GL_TEXTURE_2D);
            if(gl.rectangle_textures_){gl.GetIntegerv(rectangle_binding,&rectangle_texture);rectangle_enabled=gl.IsEnabled(rectangle);}
            gl.GetFloatv(GL_CURRENT_TEXTURE_COORDS,coords);gl.GetIntegerv(GL_VIEWPORT,viewport);
            gl.GetIntegerv(GL_POLYGON_MODE,polygon);gl.GetBooleanv(GL_COLOR_WRITEMASK,color);
            for(unsigned i=0;i<6;++i)enabled[i]=gl.IsEnabled(caps[i]);
        }
        void prepare(int w,int h){
            for(unsigned i=0;i<6;++i)if(enabled[i])gl.Disable(caps[i]);
            color_changed=!color[0]||!color[1]||!color[2]||!color[3];
            if(color_changed)gl.ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            polygon_changed=polygon[0]!=GL_FILL||polygon[1]!=GL_FILL;
            if(polygon_changed)gl.PolygonMode(GL_FRONT_AND_BACK,GL_FILL);
            viewport_changed=viewport[0]||viewport[1]||viewport[2]!=w||viewport[3]!=h;
            if(viewport_changed)gl.Viewport(0,0,w,h);
        }
        ~DrawState(){
            if(bound)SDL_GL_UnbindTexture(gl.texture_.get());
            gl.UseProgram(GLuint(program));
            if(viewport_changed)gl.Viewport(viewport[0],viewport[1],viewport[2],viewport[3]);
            if(polygon_changed){
                if(polygon[0]==polygon[1])gl.PolygonMode(GL_FRONT_AND_BACK,GLenum(polygon[0]));
                else{gl.PolygonMode(GL_FRONT,GLenum(polygon[0]));gl.PolygonMode(GL_BACK,GLenum(polygon[1]));}
            }
            if(color_changed)gl.ColorMask(color[0],color[1],color[2],color[3]);
            for(unsigned i=0;i<6;++i)if(enabled[i])gl.Enable(caps[i]);
            gl.TexCoord4fv(coords);gl.BindTexture(GL_TEXTURE_2D,GLuint(texture));
            (texture_enabled?gl.Enable:gl.Disable)(GL_TEXTURE_2D);
            if(gl.rectangle_textures_){gl.BindTexture(rectangle,GLuint(rectangle_texture));(rectangle_enabled?gl.Enable:gl.Disable)(rectangle);}
            if(active!=GL_TEXTURE0)gl.ActiveTexture(GLenum(active));
        }
    };
    GLuint shader(GLenum kind,const char* source){
        GLuint id=CreateShader(kind);if(!id)return 0;ShaderSource(id,1,&source,nullptr);CompileShader(id);
        GLint ok=0;GetShaderiv(id,GL_COMPILE_STATUS,&ok);if(!ok){DeleteShader(id);return 0;}return id;
    }
public:
    explicit OpenGlLcd(SDL_Renderer* r):renderer_(r){}
    ~OpenGlLcd(){if(program_&&SDL_GL_GetCurrentContext()==context_)DeleteProgram(program_);}
    bool available(){
        if(failed_)return false;
        if(tried_)return program_!=0&&SDL_GL_GetCurrentContext()==context_;tried_=true;
        SDL_RendererInfo info{};SDL_GetRendererInfo(renderer_,&info);
        if(!info.name||std::strcmp(info.name,"opengl")||!SDL_GL_GetCurrentContext())return false;
        context_=SDL_GL_GetCurrentContext();
#ifdef __linux__
        static bool identity_logged=false;
        if(!identity_logged){
            using GetString=const GLubyte* (APIENTRY*)(GLenum);
            auto get=reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
            if(get){std::fprintf(stderr,"desktop GL vendor=%s renderer=%s version=%s\n",get(GL_VENDOR),get(GL_RENDERER),get(GL_VERSION));identity_logged=true;}
        }
#endif
#define LOAD(name) name=reinterpret_cast<name##_type>(SDL_GL_GetProcAddress("gl" #name));if(!name)return false;
        LOAD(CreateShader) LOAD(ShaderSource) LOAD(CompileShader) LOAD(GetShaderiv) LOAD(DeleteShader)
        LOAD(CreateProgram) LOAD(AttachShader) LOAD(LinkProgram) LOAD(GetProgramiv) LOAD(DeleteProgram) LOAD(UseProgram)
        LOAD(GetUniformLocation) LOAD(Uniform1i) LOAD(Uniform2f) LOAD(Uniform4f) LOAD(GetIntegerv) LOAD(GetBooleanv) LOAD(GetFloatv)
        LOAD(IsEnabled) LOAD(Enable) LOAD(BindTexture) LOAD(ActiveTexture) LOAD(Disable) LOAD(Viewport) LOAD(ColorMask) LOAD(PolygonMode)
        LOAD(Begin) LOAD(End) LOAD(TexCoord2f) LOAD(TexCoord4fv) LOAD(Vertex2f)
#undef LOAD
        rectangle_textures_=SDL_GL_ExtensionSupported("GL_ARB_texture_rectangle")||SDL_GL_ExtensionSupported("GL_EXT_texture_rectangle")||SDL_GL_ExtensionSupported("GL_NV_texture_rectangle");
        constexpr char vertex[]=R"(#version 120
varying vec2 uv;void main(){gl_Position=gl_Vertex;uv=gl_MultiTexCoord0.xy;})";
        constexpr char fragment[]=R"(#version 120
uniform sampler2D image;uniform vec4 size;uniform vec2 texture_scale;varying vec2 uv;
void main(){vec2 source=uv*size.xy,base=floor(source-0.5),f=source-0.5-base;
vec2 lo=(clamp(base,vec2(0),size.xy-1.0)+0.5)/size.xy*texture_scale;
vec2 hi=(clamp(base+1.0,vec2(0),size.xy-1.0)+0.5)/size.xy*texture_scale;
vec3 a=texture2D(image,lo).rgb,b=texture2D(image,vec2(hi.x,lo.y)).rgb;
vec3 c=texture2D(image,vec2(lo.x,hi.y)).rgb,d=texture2D(image,hi).rgb;
vec2 phase=fract(source),edges=min(phase,1.0-phase)*size.zw/size.xy;
float edge=clamp(min(edges.x,edges.y),0.0,1.0),light=0.78+0.22*edge*edge*(3.0-2.0*edge);
vec3 rgb=mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
gl_FragColor=vec4(floor(rgb*255.0*light*vec3(0.9928,1,0.9856)+0.5)/255.0,1);})";
        GLuint vs=shader(GL_VERTEX_SHADER,vertex),fs=shader(GL_FRAGMENT_SHADER,fragment);
        if(vs&&fs){program_=CreateProgram();AttachShader(program_,vs);AttachShader(program_,fs);LinkProgram(program_);GLint ok=0;GetProgramiv(program_,GL_LINK_STATUS,&ok);if(!ok){DeleteProgram(program_);program_=0;}}
        if(vs)DeleteShader(vs);if(fs)DeleteShader(fs);
        if(program_){
            GLint image=GetUniformLocation(program_,"image");size_uniform_=GetUniformLocation(program_,"size");texture_scale_uniform_=GetUniformLocation(program_,"texture_scale");
            if(image<0||size_uniform_<0||texture_scale_uniform_<0){DeleteProgram(program_);program_=0;}
            else{GLint previous=0;GetIntegerv(GL_CURRENT_PROGRAM,&previous);UseProgram(program_);Uniform1i(image,0);UseProgram(GLuint(previous));}
        }
        return program_!=0;
    }
    bool draw(const gbcllr::PresentationFrame& frame,const SDL_Rect& dest){
        // Match the portable sampler's density cap and the D3D9 contract.
        if(!available()||!frame.width||!frame.height||frame.pixels.size()!=size_t(frame.width)*frame.height||dest.w<1||dest.h<1||dest.w>4096||dest.h>4096)return false;
        if(SDL_RenderFlush(renderer_))return false;
        if(width_!=frame.width||height_!=frame.height){
            texture_.reset(SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,frame.width,frame.height));
            if(!texture_)return false;SDL_SetTextureScaleMode(texture_.get(),SDL_ScaleModeNearest);
            width_=frame.width;height_=frame.height;previous_.clear();
        }
        if(previous_.size()!=frame.pixels.size()||!std::equal(previous_.begin(),previous_.end(),frame.pixels.begin())){
            if(SDL_UpdateTexture(texture_.get(),nullptr,frame.pixels.data(),frame.width*4))return false;
            previous_.assign(frame.pixels.begin(),frame.pixels.end());bytes_+=frame.pixels.size()*4;
        }
        DrawState state(*this);
        float tw=1,th=1;if(SDL_GL_BindTexture(texture_.get(),&tw,&th))return false;state.bound=true;
        int w=0,h=0;if(SDL_GetRendererOutputSize(renderer_,&w,&h)||!w||!h)return false;
        state.prepare(w,h);UseProgram(program_);
        Uniform4f(size_uniform_,frame.width,frame.height,dest.w,dest.h);Uniform2f(texture_scale_uniform_,tw,th);
        float l=2.0f*dest.x/w-1,r=2.0f*(dest.x+dest.w)/w-1,t=1-2.0f*dest.y/h,b=1-2.0f*(dest.y+dest.h)/h;
        Begin(GL_TRIANGLE_STRIP);TexCoord2f(0,0);Vertex2f(l,t);TexCoord2f(0,1);Vertex2f(l,b);TexCoord2f(1,0);Vertex2f(r,t);TexCoord2f(1,1);Vertex2f(r,b);End();
        ++count_;return true;
    }
    void invalidate(){failed_=true;texture_.reset();previous_.clear();width_=height_=0;}
    void reset(){failed_=false;texture_.reset();previous_.clear();width_=height_=0;
        if(context_!=SDL_GL_GetCurrentContext()){program_=0;tried_=false;context_=nullptr;}}
    uint64_t draws()const{return count_;}uint64_t upload_bytes()const{return bytes_;}
};
