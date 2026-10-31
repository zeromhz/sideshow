// Adapted from Pavel Dobryakov, MIT. See third_party/fluid/LICENSE and README.md.
#include "fluid_shaders.h"
namespace sideshow::fluid_shader {
const char* vertex=R"GLSL(out vec2 vUv, vL, vR, vT, vB;
uniform vec2 texelSize;
void main() {
    vec2 pos=vec2(float((gl_VertexID & 1)<<2)-1.0,float((gl_VertexID & 2)<<1)-1.0);
    vUv=pos*.5+.5;
    vL=vUv-vec2(texelSize.x,0);vR=vUv+vec2(texelSize.x,0);
    vT=vUv+vec2(0,texelSize.y);vB=vUv-vec2(0,texelSize.y);
    gl_Position=vec4(pos,0,1);
})GLSL";
const char* blurVertex=R"GLSL(out vec2 vUv, vL, vR, vT, vB;
uniform vec2 texelSize;
void main() {
    vec2 pos=vec2(float((gl_VertexID & 1)<<2)-1.0,float((gl_VertexID & 2)<<1)-1.0);
    vUv=pos*.5+.5;
    vL=vUv-texelSize*1.33333333;vR=vUv+texelSize*1.33333333;
    vT=vUv+vec2(0,texelSize.y);vB=vUv-vec2(0,texelSize.y);
    gl_Position=vec4(pos,0,1);
})GLSL";
const char* copy=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    uniform sampler2D uTexture;

    void main () {
        fragColor = texture(uTexture, vUv);
    }
)GLSL";
const char* clear=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    uniform sampler2D uTexture;
    uniform float value;

    void main () {
        fragColor = value * texture(uTexture, vUv);
    }
)GLSL";
const char* splat=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    uniform sampler2D uTarget;
    uniform float aspectRatio;
    uniform vec3 color;
    uniform vec2 point;
    uniform float radius;

    void main () {
        vec2 p = vUv - point.xy;
        p.x *= aspectRatio;
        vec3 splat = exp(-dot(p, p) / radius) * color;
        vec3 base = texture(uTarget, vUv).xyz;
        fragColor = vec4(base + splat, 1.0);
    }
)GLSL";
const char* advection=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    uniform sampler2D uVelocity;
    uniform sampler2D uSource;
    uniform vec2 texelSize;
    uniform vec2 dyeTexelSize;
    uniform float dt;
    uniform float dissipation;

    vec4 bilerp (sampler2D sam, vec2 uv, vec2 tsize) {
        vec2 st = uv / tsize - 0.5;

        vec2 iuv = floor(st);
        vec2 fuv = fract(st);

        vec4 a = texture(sam, (iuv + vec2(0.5, 0.5)) * tsize);
        vec4 b = texture(sam, (iuv + vec2(1.5, 0.5)) * tsize);
        vec4 c = texture(sam, (iuv + vec2(0.5, 1.5)) * tsize);
        vec4 d = texture(sam, (iuv + vec2(1.5, 1.5)) * tsize);

        return mix(mix(a, b, fuv.x), mix(c, d, fuv.x), fuv.y);
    }

    void main () {
    #ifdef MANUAL_FILTERING
        vec2 coord = vUv - dt * bilerp(uVelocity, vUv, texelSize).xy * texelSize;
        vec4 result = bilerp(uSource, coord, dyeTexelSize);
    #else
        vec2 coord = vUv - dt * texture(uVelocity, vUv).xy * texelSize;
        vec4 result = texture(uSource, coord);
    #endif
        float decay = 1.0 + dissipation * dt;
        fragColor = result / decay;
    })GLSL";
const char* divergence=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    in vec2 vL;
    in vec2 vR;
    in vec2 vT;
    in vec2 vB;
    uniform sampler2D uVelocity;

    void main () {
        float L = texture(uVelocity, vL).x;
        float R = texture(uVelocity, vR).x;
        float T = texture(uVelocity, vT).y;
        float B = texture(uVelocity, vB).y;

        vec2 C = texture(uVelocity, vUv).xy;
        if (vL.x < 0.0) { L = -C.x; }
        if (vR.x > 1.0) { R = -C.x; }
        if (vT.y > 1.0) { T = -C.y; }
        if (vB.y < 0.0) { B = -C.y; }

        float div = 0.5 * (R - L + T - B);
        fragColor = vec4(div, 0.0, 0.0, 1.0);
    }
)GLSL";
const char* curl=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    in vec2 vL;
    in vec2 vR;
    in vec2 vT;
    in vec2 vB;
    uniform sampler2D uVelocity;

    void main () {
        float L = texture(uVelocity, vL).y;
        float R = texture(uVelocity, vR).y;
        float T = texture(uVelocity, vT).x;
        float B = texture(uVelocity, vB).x;
        float vorticity = R - L - T + B;
        fragColor = vec4(0.5 * vorticity, 0.0, 0.0, 1.0);
    }
)GLSL";
const char* vorticity=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    in vec2 vL;
    in vec2 vR;
    in vec2 vT;
    in vec2 vB;
    uniform sampler2D uVelocity;
    uniform sampler2D uCurl;
    uniform float curl;
    uniform float dt;

    void main () {
        float L = texture(uCurl, vL).x;
        float R = texture(uCurl, vR).x;
        float T = texture(uCurl, vT).x;
        float B = texture(uCurl, vB).x;
        float C = texture(uCurl, vUv).x;

        vec2 force = 0.5 * vec2(abs(T) - abs(B), abs(R) - abs(L));
        force /= length(force) + 0.0001;
        force *= curl * C;
        force.y *= -1.0;

        vec2 velocity = texture(uVelocity, vUv).xy;
        velocity += force * dt;
        velocity = min(max(velocity, -1000.0), 1000.0);
        fragColor = vec4(velocity, 0.0, 1.0);
    }
)GLSL";
const char* pressure=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    in vec2 vL;
    in vec2 vR;
    in vec2 vT;
    in vec2 vB;
    uniform sampler2D uPressure;
    uniform sampler2D uDivergence;

    void main () {
        float L = texture(uPressure, vL).x;
        float R = texture(uPressure, vR).x;
        float T = texture(uPressure, vT).x;
        float B = texture(uPressure, vB).x;
        float C = texture(uPressure, vUv).x;
        float divergence = texture(uDivergence, vUv).x;
        float pressure = (L + R + B + T - divergence) * 0.25;
        fragColor = vec4(pressure, 0.0, 0.0, 1.0);
    }
)GLSL";
const char* gradientSubtract=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    in vec2 vL;
    in vec2 vR;
    in vec2 vT;
    in vec2 vB;
    uniform sampler2D uPressure;
    uniform sampler2D uVelocity;

    void main () {
        float L = texture(uPressure, vL).x;
        float R = texture(uPressure, vR).x;
        float T = texture(uPressure, vT).x;
        float B = texture(uPressure, vB).x;
        vec2 velocity = texture(uVelocity, vUv).xy;
        velocity.xy -= vec2(R - L, T - B);
        fragColor = vec4(velocity, 0.0, 1.0);
    }
)GLSL";
const char* bloomPrefilter=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    uniform sampler2D uTexture;
    uniform vec3 curve;
    uniform float threshold;

    void main () {
        vec3 c = texture(uTexture, vUv).rgb;
        float br = max(c.r, max(c.g, c.b));
        float rq = clamp(br - curve.x, 0.0, curve.y);
        rq = curve.z * rq * rq;
        c *= max(rq, br - threshold) / max(br, 0.0001);
        fragColor = vec4(c, 0.0);
    }
)GLSL";
const char* bloomBlur=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vL;
    in vec2 vR;
    in vec2 vT;
    in vec2 vB;
    uniform sampler2D uTexture;

    void main () {
        vec4 sum = vec4(0.0);
        sum += texture(uTexture, vL);
        sum += texture(uTexture, vR);
        sum += texture(uTexture, vT);
        sum += texture(uTexture, vB);
        sum *= 0.25;
        fragColor = sum;
    }
)GLSL";
const char* bloomFinal=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vL;
    in vec2 vR;
    in vec2 vT;
    in vec2 vB;
    uniform sampler2D uTexture;
    uniform float intensity;

    void main () {
        vec4 sum = vec4(0.0);
        sum += texture(uTexture, vL);
        sum += texture(uTexture, vR);
        sum += texture(uTexture, vT);
        sum += texture(uTexture, vB);
        sum *= 0.25;
        fragColor = sum * intensity;
    }
)GLSL";
const char* sunraysMask=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    uniform sampler2D uTexture;

    void main () {
        vec4 c = texture(uTexture, vUv);
        float br = max(c.r, max(c.g, c.b));
        c.a = 1.0 - min(max(br * 20.0, 0.0), 0.8);
        fragColor = c;
    }
)GLSL";
const char* sunrays=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    uniform sampler2D uTexture;
    uniform float weight;

    #define ITERATIONS 16

    void main () {
        float Density = 0.3;
        float Decay = 0.95;
        float Exposure = 0.7;

        vec2 coord = vUv;
        vec2 dir = vUv - 0.5;

        dir *= 1.0 / float(ITERATIONS) * Density;
        float illuminationDecay = 1.0;

        float color = texture(uTexture, vUv).a;

        for (int i = 0; i < ITERATIONS; i++)
        {
            coord -= dir;
            float col = texture(uTexture, coord).a;
            color += col * illuminationDecay * weight;
            illuminationDecay *= Decay;
        }

        fragColor = vec4(color * Exposure, 0.0, 0.0, 1.0);
    }
)GLSL";
const char* blur=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    in vec2 vL;
    in vec2 vR;
    uniform sampler2D uTexture;

    void main () {
        vec4 sum = texture(uTexture, vUv) * 0.29411764;
        sum += texture(uTexture, vL) * 0.35294117;
        sum += texture(uTexture, vR) * 0.35294117;
        fragColor = sum;
    }
)GLSL";
// Stretches the dye-resolution composite over the screen and dithers it by one 8-bit step, so
// smooth glows do not band.
const char* present=R"GLSL(out vec4 fragColor;
    in vec2 vUv;
    uniform sampler2D uTexture;
    uniform sampler2D uDithering;
    uniform vec2 ditherScale;
    uniform float ditherAmount;

    void main () {
        vec3 c = texture(uTexture, vUv).rgb;
        float noise = texture(uDithering, vUv * ditherScale).r * 2.0 - 1.0;
        fragColor = vec4(c + ditherAmount * noise / 255.0, 1.0);
    }
)GLSL";
const char* display=R"GLSL(out vec4 fragColor;

    
    

    in vec2 vUv;
    in vec2 vL;
    in vec2 vR;
    in vec2 vT;
    in vec2 vB;
    uniform float uInvert;
    uniform sampler2D uTexture;
    uniform sampler2D uBloom;
    uniform sampler2D uSunrays;
    uniform sampler2D uDithering;
    uniform vec2 ditherScale;
    uniform vec2 texelSize;

    vec3 linearToGamma (vec3 color) {
        color = max(color, vec3(0));
        return max(1.055 * pow(color, vec3(0.416666667)) - 0.055, vec3(0));
    }

    void main () {
        vec3 c = texture(uTexture, vUv).rgb;

    #ifdef SHADING
        vec3 lc = texture(uTexture, vL).rgb;
        vec3 rc = texture(uTexture, vR).rgb;
        vec3 tc = texture(uTexture, vT).rgb;
        vec3 bc = texture(uTexture, vB).rgb;

        float dx = length(rc) - length(lc);
        float dy = length(tc) - length(bc);

        vec3 n = normalize(vec3(dx, dy, length(texelSize)));
        vec3 l = vec3(0.0, 0.0, 1.0);

        float diffuse = clamp(dot(n, l) + 0.7, 0.7, 1.0);
        c *= diffuse;
    #endif

    #ifdef BLOOM
        vec3 bloom = texture(uBloom, vUv).rgb;
    #endif

    #ifdef SUNRAYS
        float sunrays = texture(uSunrays, vUv).r;
        c *= sunrays;
    #ifdef BLOOM
        bloom *= sunrays;
    #endif
    #endif

    #ifdef BLOOM
        // Dithering happens in the present pass, at screen resolution.
        bloom = linearToGamma(bloom);
        c += bloom;
    #endif

        float a = max(c.r, max(c.g, c.b));
        fragColor = vec4(mix(c, vec3(1.0)-clamp(c,0.0,1.0),uInvert), 1.0);
    }
)GLSL";
}
