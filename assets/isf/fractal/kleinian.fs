/*{"ISFVSN":"2.0","DESCRIPTION":"Original Sideshow Apollonian circle packing built from repeated circle inversion","CREDIT":"Sideshow contributors","CATEGORIES":["Generator","Fractal"],"INPUTS":[{"NAME":"spectrum","TYPE":"audioFFT","MAX":256},{"NAME":"speed","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":4.0},{"NAME":"zoom","TYPE":"float","DEFAULT":1.0,"MIN":0.5,"MAX":4.0},{"NAME":"color","TYPE":"float","DEFAULT":0.0,"MIN":0.0,"MAX":1.0},{"NAME":"detail","TYPE":"float","DEFAULT":0.5,"MIN":0.0,"MAX":1.0},{"NAME":"reactivity","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":3.0}],"PASSES":[{"TARGET":"state","PERSISTENT":true,"WIDTH":4,"HEIGHT":1},{}]}*/
precision highp float;

// Each step folds the plane into a unit cell, then inverts it in a circle. Circles map to
// circles under inversion, so the cell's edges become ever smaller nested circles: an
// Apollonian-style packing. A Moebius map of the view (another inversion) bends the packing
// around a drifting pole, so the camera seems to fall into a ring of circles.

// Pass 0 keeps animation state in a 4x1 RGBA8 target, one 32-bit fixed-point value per pixel:
// 0 drift phase, 1 bass envelope, 2 level envelope, 3 hue phase.

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

vec3 palette(float t) { return 0.5 + 0.5 * cos(6.2831853 * (t + vec3(0.0, 0.33, 0.67))); }

void main() {
    if (PASSINDEX == 0) {
        float index = floor(gl_FragCoord.x);
        float dt = FRAMEINDEX == 0 ? 0.0 : clamp(TIMEDELTA, 0.0, 0.1);
        float bass = envelope(readState(1.0), band(1.0, 4.0) * reactivity, dt);
        float value;
        if (index < 0.5) value = fract(readState(0.0) + dt * speed * 0.01 * (1.0 + 1.5 * bass));
        else if (index < 1.5) value = bass;
        else if (index < 2.5) value = envelope(readState(2.0), band(4.0, 96.0) * reactivity, dt);
        else value = fract(readState(3.0) + dt * (0.01 + 0.08 * bass));
        gl_FragColor = FRAMEINDEX == 0 ? vec4(0.0) : packUnit(min(value, 0.999));
        return;
    }

    float phase = 6.2831853 * readState(0.0);
    float bass = readState(1.0);
    float level = readState(2.0);
    float hue = color + readState(3.0);

    vec2 uv = (gl_FragCoord.xy - 0.5 * RENDERSIZE) / (0.5 * RENDERSIZE.y) / zoom;
    // Moebius view: invert around a pole that circles the screen, then drift through the cell.
    vec2 pole = 0.6 * vec2(cos(phase), sin(phase * 2.0));
    vec2 q = uv - pole;
    vec2 p = q / dot(q, q) + vec2(phase * 2.0 / 3.1415927, 0.37);
    float scale = 1.0 / dot(q, q);

    // The inversion radius sets how tightly circles pack; bass swells it.
    float radius = 1.15 + 0.1 * sin(phase * 3.0) + 0.12 * bass;
    int steps = 4 + int(detail * 8.0);
    float trap = 1e9;
    for (int i = 0; i < 12; i++) {
        if (i >= steps) break;
        p = -1.0 + 2.0 * fract(0.5 * p + 0.5);
        float r2 = dot(p, p);
        trap = min(trap, r2);
        float k = radius / r2;
        p *= k;
        scale *= k;
    }
    // Distance (in view units) to the nearest circle edge, and its size in pixels.
    float edge = 0.25 * abs(p.y) / scale;
    float pixel = 2.0 / (RENDERSIZE.y * zoom);
    float line = exp(-edge / pixel * 1.5);
    float glow = exp(-edge / pixel * 0.25);

    // Shade each disc by how far the inversions shrank it, so nested circles step through
    // the palette from the outside in.
    float shade = log2(scale);
    vec3 fill = palette(hue + 0.06 * shade) * (0.12 + 0.2 * smoothstep(0.0, 1.0, trap));
    vec3 col = fill + palette(hue + 0.5 + 0.06 * shade) * glow * 0.4;
    col += mix(vec3(1.0), palette(hue + 0.25 + 0.06 * shade), 0.5) * line * (0.8 + 0.8 * level);
    gl_FragColor = vec4(col, 1.0);
}
