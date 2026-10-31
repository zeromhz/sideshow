/*{"ISFVSN":"2.0","DESCRIPTION":"Original Sideshow metallic Julia set whose parameter orbits the Mandelbrot cardioid","CREDIT":"Sideshow contributors","CATEGORIES":["Generator","Fractal"],"INPUTS":[{"NAME":"spectrum","TYPE":"audioFFT","MAX":256},{"NAME":"speed","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":4.0},{"NAME":"zoom","TYPE":"float","DEFAULT":1.0,"MIN":0.5,"MAX":4.0},{"NAME":"color","TYPE":"float","DEFAULT":0.0,"MIN":0.0,"MAX":1.0},{"NAME":"detail","TYPE":"float","DEFAULT":0.4,"MIN":0.0,"MAX":1.0},{"NAME":"reactivity","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":3.0}],"PASSES":[{"TARGET":"state","PERSISTENT":true,"WIDTH":4,"HEIGHT":1},{}]}*/
precision highp float;

// Pass 0 keeps animation state in a 4x1 RGBA8 target, one 32-bit fixed-point value per pixel:
// 0 orbit phase, 1 bass envelope, 2 level envelope, 3 hue phase. Integrating phases here
// lets speed and audio change smoothly instead of jumping like TIME * speed would.

vec4 packUnit(float v) {
    vec4 e = fract(vec4(1.0, 255.0, 65025.0, 16581375.0) * clamp(v, 0.0, 0.99999994));
    return e - e.yzww * vec4(1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0);
}
float unpackUnit(vec4 e) { return dot(e, vec4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 1.0 / 16581375.0)); }
float readState(float index) { return unpackUnit(IMG_PIXEL(state, vec2(index + 0.5, 0.5))); }

float band(float first, float last) {
    float sum = 0.0;
    for (int i = 0; i < 8; i++) {
        float bin = floor(mix(first, last, float(i) / 7.0)) + 0.5;
        sum += max(IMG_PIXEL(spectrum, vec2(bin, 0.5)).r, IMG_PIXEL(spectrum, vec2(bin, 1.5)).r);
    }
    return 1.0 - exp(-sum * 3.0);
}

float envelope(float previous, float target, float dt) {
    return target > previous ? mix(previous, target, 0.6) : previous * exp(-dt * 3.5);
}

vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 cdiv(vec2 a, vec2 b) { return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / max(dot(b, b), 1e-30); }
vec3 palette(float t) { return 0.5 + 0.5 * cos(6.2831853 * (t + vec3(0.0, 0.33, 0.67))); }

void main() {
    if (PASSINDEX == 0) {
        float index = floor(gl_FragCoord.x);
        float dt = FRAMEINDEX == 0 ? 0.0 : clamp(TIMEDELTA, 0.0, 0.1);
        float bass = envelope(readState(1.0), band(1.0, 4.0) * reactivity, dt);
        float value;
        if (index < 0.5) value = fract(readState(0.0) + dt * speed * 0.02 * (1.0 + 1.5 * bass));
        else if (index < 1.5) value = bass;
        else if (index < 2.5) value = envelope(readState(2.0), band(4.0, 96.0) * reactivity, dt);
        else value = fract(readState(3.0) + dt * (0.01 + 0.08 * bass));
        gl_FragColor = FRAMEINDEX == 0 ? vec4(0.0) : packUnit(min(value, 0.999));
        return;
    }

    float phase = readState(0.0);
    float bass = readState(1.0);
    float level = readState(2.0);
    float hue = color + readState(3.0);

    // c circles the origin at a radius where the Julia set flips between spirals, dendrites
    // and dust; bass pushes it outward so the set shatters on hits.
    float angle = 6.2831853 * phase;
    vec2 c = vec2(cos(angle), sin(angle)) * (0.7885 + 0.012 * sin(angle * 5.0) + 0.04 * bass);

    float halfHeight = 1.2 / zoom;
    vec2 uv = (gl_FragCoord.xy - 0.5 * RENDERSIZE) / (0.5 * RENDERSIZE.y);
    float spin = angle * 0.5;
    vec2 z = cmul(uv * halfHeight, vec2(cos(spin), sin(spin)));
    vec2 dz = vec2(1.0, 0.0);
    int limit = 32 + int(detail * 224.0);
    float trap = 1e9;
    float n = 0.0;
    float m = 0.0;
    bool escaped = false;
    for (int i = 0; i < 256; i++) {
        if (i >= limit) break;
        dz = 2.0 * cmul(z, dz);
        z = cmul(z, z) + c;
        m = dot(z, z);
        trap = min(trap, m);
        if (m > 1e4) { escaped = true; n = float(i); break; }
        if (dot(dz, dz) > 1e30) break;
    }

    vec3 col;
    if (escaped) {
        // Polished metal: the escape gradient direction z/dz gives each point a surface normal.
        float smoothCount = n + 1.0 - log2(0.5 * log2(m));
        vec2 u = normalize(cdiv(z, dz));
        vec3 normal = normalize(vec3(u, 0.6));
        vec3 light = normalize(vec3(cos(angle * 3.0), sin(angle * 3.0), 1.0));
        vec3 reflected = reflect(vec3(0.0, 0.0, -1.0), normal);
        vec3 base = palette(hue + 0.06 * smoothCount);
        float diffuse = max(dot(normal, light), 0.0);
        float specular = pow(max(dot(reflected, light), 0.0), 16.0);
        float horizon = smoothstep(-0.5, 0.0, reflected.y) - 0.5 * smoothstep(0.0, 0.8, reflected.y);
        vec3 metal = mix(base * 0.5, mix(base, vec3(1.0), 0.5), horizon);
        col = metal * (0.55 + 0.6 * diffuse) + vec3(1.0, 0.95, 0.88) * specular * (0.7 + 1.5 * level);
        // Fade toward the open background so the set reads as an object, and outline it
        // with the distance estimate for clean edges.
        float distance = 0.5 * sqrt(m / max(dot(dz, dz), 1e-30)) * log(m);
        col *= 0.06 + 0.94 * exp(-distance / (0.18 * halfHeight));
        col *= mix(0.3, 1.0, smoothstep(0.0, 1.5, distance / (2.0 * halfHeight / RENDERSIZE.y)));
    } else {
        col = palette(hue + 0.5 + 0.3 * sqrt(trap)) * (0.015 + 0.05 * level);
    }
    gl_FragColor = vec4(sqrt(col * (0.9 + 0.35 * level)), 1.0);
}
