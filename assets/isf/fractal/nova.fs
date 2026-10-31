/*{"ISFVSN":"2.0","DESCRIPTION":"Original Sideshow metallic Nova fractal: relaxed Newton basins that spin and bloom","CREDIT":"Sideshow contributors","CATEGORIES":["Generator","Fractal"],"INPUTS":[{"NAME":"spectrum","TYPE":"audioFFT","MAX":256},{"NAME":"speed","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":4.0},{"NAME":"zoom","TYPE":"float","DEFAULT":1.0,"MIN":0.5,"MAX":4.0},{"NAME":"color","TYPE":"float","DEFAULT":0.0,"MIN":0.0,"MAX":1.0},{"NAME":"detail","TYPE":"float","DEFAULT":0.4,"MIN":0.0,"MAX":1.0},{"NAME":"reactivity","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":3.0}],"PASSES":[{"TARGET":"state","PERSISTENT":true,"WIDTH":4,"HEIGHT":1},{}]}*/
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

    // Nova map: a relaxed Newton step for z^3 = 1 plus a constant, z' = z - R (z^3 - 1) / 3z^2 + c.
    // c circles slowly, so the three-fold basins spin and bloom; bass over-relaxes the step and
    // throws filigree out of the basin boundaries.
    vec2 c = 0.12 * vec2(cos(angle), sin(angle)) * (0.8 + 0.25 * sin(angle * 3.0));
    float relax = 1.0 + 0.25 * sin(angle * 2.0) + 0.35 * bass;

    float halfHeight = 2.6 / zoom;
    vec2 uv = (gl_FragCoord.xy - 0.5 * RENDERSIZE) / (0.5 * RENDERSIZE.y) * halfHeight;
    float spin = -angle;
    vec2 z = cmul(uv, vec2(cos(spin), sin(spin)));
    vec2 dz = vec2(1.0, 0.0);
    vec2 step = vec2(1.0, 0.0);
    vec2 dStep = vec2(0.0);
    int limit = 24 + int(detail * 104.0);
    float n = float(limit);
    bool settled = false;
    for (int i = 0; i < 128; i++) {
        if (i >= limit) break;
        vec2 z2 = cmul(z, z);
        vec2 z3 = cmul(z2, z);
        // f(z) = z - R (z/3 - 1 / 3z^2) + c,   f'(z) = 1 - R (1/3 + 2 / 3z^3)
        vec2 inverse2 = cdiv(vec2(1.0, 0.0), z2);
        vec2 next = z - relax * (z / 3.0 - inverse2 / 3.0) + c;
        vec2 slope = vec2(1.0, 0.0) - relax * (vec2(1.0 / 3.0, 0.0) + 2.0 / 3.0 * cdiv(vec2(1.0, 0.0), z3));
        vec2 dNext = cmul(slope, dz);
        step = next - z; dStep = dNext - dz;
        z = next; dz = dNext;
        if (dot(step, step) < 1e-7) { settled = true; n = float(i); break; }
    }

    // Smooth convergence count, from how far the last step fell below the threshold.
    float stepSize = max(dot(step, step), 1e-30);
    float smoothCount = n - log2(max(log(stepSize) / log(1e-7), 1e-6));
    // The settling direction picks the basin; its gradient gives the metal a surface normal.
    float basin = atan(z.y, z.x) / 6.2831853;
    vec2 u = normalize(cdiv(step, dStep));
    vec3 normal = normalize(vec3(u, 0.8));
    vec3 light = normalize(vec3(cos(angle * 2.0), sin(angle * 2.0), 1.0));
    vec3 reflected = reflect(vec3(0.0, 0.0, -1.0), normal);
    vec3 base = palette(hue + basin + 0.02 * smoothCount);
    float diffuse = max(dot(normal, light), 0.0);
    float specular = pow(max(dot(reflected, light), 0.0), 20.0);
    float horizon = smoothstep(-0.5, 0.0, reflected.y) - 0.5 * smoothstep(0.0, 0.8, reflected.y);
    vec3 col = mix(base * 0.45, mix(base, vec3(1.0), 0.5), horizon) * (0.5 + 0.6 * diffuse)
             + vec3(1.0, 0.95, 0.88) * specular * (0.6 + 1.5 * level);
    // Distance to the nearest basin boundary, estimated from how the final step varies across
    // the plane: basin cores fall away to dark metal and the tangled boundaries shine.
    float distance = sqrt(stepSize / max(dot(dStep, dStep), 1e-30)) * -log(stepSize) * 0.1;
    col *= 0.05 + 0.95 * exp(-distance / (0.1 * halfHeight));
    if (!settled) col = palette(hue + 0.5) * (0.03 + 0.08 * level);
    gl_FragColor = vec4(sqrt(col * (0.9 + 0.35 * level)), 1.0);
}
