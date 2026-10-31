/*{"ISFVSN":"2.0","DESCRIPTION":"Original Sideshow metallic Multibrot (z^d + c) that weaves between the set and its Julia sets","CREDIT":"Sideshow contributors","CATEGORIES":["Generator","Fractal"],"INPUTS":[{"NAME":"spectrum","TYPE":"audioFFT","MAX":256},{"NAME":"speed","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":4.0},{"NAME":"zoom","TYPE":"float","DEFAULT":1.0,"MIN":0.5,"MAX":4.0},{"NAME":"color","TYPE":"float","DEFAULT":0.0,"MIN":0.0,"MAX":1.0},{"NAME":"detail","TYPE":"float","DEFAULT":0.4,"MIN":0.0,"MAX":1.0},{"NAME":"power","TYPE":"float","DEFAULT":3.0,"MIN":2.0,"MAX":6.0},{"NAME":"weave","TYPE":"float","DEFAULT":-1.0,"MIN":-1.0,"MAX":1.0},{"NAME":"reactivity","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":3.0}],"PASSES":[{"TARGET":"state","PERSISTENT":true,"WIDTH":4,"HEIGHT":1},{}]}*/
precision highp float;

// Pass 0 keeps animation state in a 4x1 RGBA8 target, one 32-bit fixed-point value per pixel:
// 0 morph phase, 1 bass envelope, 2 level envelope, 3 hue phase. Integrating phases here
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

// Advances the shared state. phaseRate is the morph phase change per second at speed 1.
vec4 updateState(float phaseRate) {
    float index = floor(gl_FragCoord.x);
    float dt = FRAMEINDEX == 0 ? 0.0 : clamp(TIMEDELTA, 0.0, 0.1);
    float bass = envelope(readState(1.0), band(1.0, 4.0) * reactivity, dt);
    float value;
    if (index < 0.5) value = fract(readState(0.0) + dt * speed * phaseRate * (1.0 + 1.5 * bass));
    else if (index < 1.5) value = bass;
    else if (index < 2.5) value = envelope(readState(2.0), band(4.0, 96.0) * reactivity, dt);
    else value = fract(readState(3.0) + dt * (0.01 + 0.08 * bass));
    return FRAMEINDEX == 0 ? vec4(0.0) : packUnit(min(value, 0.999));
}

vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 cdiv(vec2 a, vec2 b) { return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / max(dot(b, b), 1e-30); }
vec3 palette(float t) { return 0.5 + 0.5 * cos(6.2831853 * (t + vec3(0.0, 0.33, 0.67))); }

// Polished-metal shading for an escaped point. The escape gradient direction z/dz gives each
// point a surface normal; the distance estimate fades the open background and draws clean edges.
vec3 metal(vec2 z, vec2 dz, float m, float smoothCount, float hue, float lightAngle, float level, float halfHeight) {
    vec2 u = normalize(cdiv(z, dz));
    vec3 normal = normalize(vec3(u, 0.6));
    vec3 light = normalize(vec3(cos(lightAngle), sin(lightAngle), 1.0));
    vec3 reflected = reflect(vec3(0.0, 0.0, -1.0), normal);
    vec3 base = palette(hue + 0.06 * smoothCount);
    float diffuse = max(dot(normal, light), 0.0);
    float specular = pow(max(dot(reflected, light), 0.0), 16.0);
    float horizon = smoothstep(-0.5, 0.0, reflected.y) - 0.5 * smoothstep(0.0, 0.8, reflected.y);
    vec3 col = mix(base * 0.5, mix(base, vec3(1.0), 0.5), horizon) * (0.55 + 0.6 * diffuse)
             + vec3(1.0, 0.95, 0.88) * specular * (0.7 + 1.5 * level);
    float distance = 0.5 * sqrt(m / max(dot(dz, dz), 1e-30)) * log(m);
    col *= 0.06 + 0.94 * exp(-distance / (0.18 * halfHeight));
    col *= mix(0.3, 1.0, smoothstep(0.0, 1.5, distance / (2.0 * halfHeight / RENDERSIZE.y)));
    return col;
}

void main() {
    if (PASSINDEX == 0) { gl_FragColor = updateState(0.012); return; }

    float phase = readState(0.0);
    float bass = readState(1.0);
    float level = readState(2.0);
    float hue = color + readState(3.0);
    float angle = 6.2831853 * phase;

    // Multibrot: z' = z^d + c. power sets d (whole numbers give the classic (d-1)-fold sets;
    // values in between tear along the negative real axis). The weave slides between the set
    // itself (c = pixel, weave 0) and its Julia sets (z0 = pixel, weave 1), whose c circles the
    // main body; weave -1 lets it breathe between the two on its own. Bass pushes c outward.
    float d = power;
    float w = weave < 0.0 ? 0.5 - 0.5 * cos(angle) : weave;
    float radius = d < 2.5 ? 0.7885 : 0.62 + 0.04 * d;
    vec2 juliaC = vec2(cos(angle * 2.0), sin(angle * 2.0)) * (radius + 0.03 * sin(angle * 7.0) + 0.05 * bass);
    float halfHeight = 1.5 / zoom;
    vec2 center = vec2(d < 2.5 ? -0.5 * (1.0 - w) : 0.0, 0.0);
    vec2 uv = (gl_FragCoord.xy - 0.5 * RENDERSIZE) / (0.5 * RENDERSIZE.y);
    // Turn the set slowly; its symmetry makes the rotation read as a slow kaleidoscope.
    float spin = angle / max(d - 1.0, 1.0);
    vec2 pixel = center + cmul(uv * halfHeight, vec2(cos(spin), sin(spin)));

    vec2 z = w * pixel;
    vec2 c = mix(pixel, juliaC, w);
    vec2 dz = vec2(w, 0.0);
    vec2 dc = vec2(1.0 - w, 0.0);
    int limit = 32 + int(detail * 224.0);
    float trap = 1e9;
    float n = 0.0;
    float m = 0.0;
    bool escaped = false;
    bool whole = abs(d - floor(d + 0.5)) < 0.001;
    if (whole) d = floor(d + 0.5);
    // Periodicity check (Brent): points inside the set never escape and would otherwise run to
    // the iteration limit. An orbit that comes back to a saved point is caught in a cycle; stop.
    vec2 saved = vec2(1e9);
    int check = 8;
    for (int i = 0; i < 256; i++) {
        if (i >= limit) break;
        // z^(d-1): repeated products for whole powers, polar form in between.
        vec2 zd1 = z;
        if (whole) {
            for (int k = 2; k < 6; k++) if (float(k) < d) zd1 = cmul(zd1, z);
        } else {
            float theta = atan(z.y, z.x);
            zd1 = pow(max(dot(z, z), 1e-30), 0.5 * (d - 1.0)) * vec2(cos((d - 1.0) * theta), sin((d - 1.0) * theta));
        }
        dz = d * cmul(zd1, dz) + dc;
        z = cmul(zd1, z) + c;
        m = dot(z, z);
        trap = min(trap, m);
        if (m > 1e4) { escaped = true; n = float(i); break; }
        if (dot(dz, dz) > 1e30) break;
        vec2 drift = z - saved;
        if (dot(drift, drift) < 1e-10) break;
        if (i == check) { saved = z; check *= 2; }
    }

    vec3 col;
    if (escaped) {
        float smoothCount = n + 1.0 - log(0.5 * log2(m)) / log(d);
        col = metal(z, dz, m, smoothCount, hue, angle * 3.0, level, halfHeight);
    } else {
        col = palette(hue + 0.5 + 0.3 * sqrt(trap)) * (0.015 + 0.05 * level);
    }
    gl_FragColor = vec4(sqrt(col * (0.9 + 0.35 * level)), 1.0);
}
