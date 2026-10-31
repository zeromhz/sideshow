/*{"ISFVSN":"2.0","DESCRIPTION":"Original Sideshow endless Mandelbrot zoom into a self-similar Misiurewicz point","CREDIT":"Sideshow contributors","CATEGORIES":["Generator","Fractal"],"INPUTS":[{"NAME":"spectrum","TYPE":"audioFFT","MAX":256},{"NAME":"speed","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":4.0},{"NAME":"zoom","TYPE":"float","DEFAULT":1.0,"MIN":0.5,"MAX":4.0},{"NAME":"color","TYPE":"float","DEFAULT":0.0,"MIN":0.0,"MAX":1.0},{"NAME":"detail","TYPE":"float","DEFAULT":0.4,"MIN":0.0,"MAX":1.0},{"NAME":"spin","TYPE":"float","DEFAULT":0.2,"MIN":-2.0,"MAX":2.0},{"NAME":"reactivity","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":3.0}],"PASSES":[{"TARGET":"state","PERSISTENT":true,"WIDTH":5,"HEIGHT":1},{}]}*/
precision highp float;

// The zoom never runs out of float precision because it loops. Near the Misiurewicz point
// c0 = -0.1011 - 0.9563i the Mandelbrot set is self-similar: scaling the view by the cycle
// multiplier rho (|rho| = 1.328, arg rho = -119.6 degrees) gives the same picture. The view
// descends by exactly that factor per loop, then wraps. Pixels are iterated as perturbations
// of c0's exact orbit (with rebasing), so detail stays sharp even at small view sizes.

// Pass 0 keeps animation state in a 5x1 RGBA8 target, one 32-bit fixed-point value per pixel:
// 0 zoom phase, 1 bass envelope, 2 level envelope, 3 hue phase, 4 spin phase.

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
vec2 cdiv(vec2 a, vec2 b) { return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / dot(b, b); }
vec3 palette(float t) { return 0.5 + 0.5 * cos(6.2831853 * (t + vec3(0.0, 0.33, 0.67))); }

// Orbit of c0: 0, c0, ... reaches a repelling fixed point after PREPERIOD steps.
const int PREPERIOD = 4;
vec2 orbit(int i) {
    if (i >= PREPERIOD) return vec2(-0.327586178699358, -0.577756453298948);
    if (i == 3) return vec2(0.327586178699357, 0.577756453298951);
    if (i == 2) return vec2(-1.005359779818339, -0.762932332734299);
    if (i == 1) return vec2(-0.101096363845622, -0.956286510809142);
    return vec2(0.0);
}
const float LOG_RHO = 0.283922600961574;
const float ARG_RHO = -2.086595344738711;
const float BASE_HALF_HEIGHT = 4e-6;

void main() {
    if (PASSINDEX == 0) {
        float index = floor(gl_FragCoord.x);
        float dt = FRAMEINDEX == 0 ? 0.0 : clamp(TIMEDELTA, 0.0, 0.1);
        float bass = envelope(readState(1.0), band(1.0, 4.0) * reactivity, dt);
        float value;
        // speed 1 = one e-fold of magnification every 2.5 s; bass hits surge forward.
        if (index < 0.5) value = fract(readState(0.0) + dt * speed * 0.4 * (1.0 + 2.0 * bass) / LOG_RHO);
        else if (index < 1.5) value = bass;
        else if (index < 2.5) value = envelope(readState(2.0), band(4.0, 96.0) * reactivity, dt);
        else if (index < 3.5) value = fract(readState(3.0) + dt * (0.01 + 0.08 * bass));
        else value = fract(readState(4.0) + dt * spin * 0.05 + 1.0);
        gl_FragColor = FRAMEINDEX == 0 ? vec4(0.0) : packUnit(min(value, 0.999));
        return;
    }

    float phase = readState(0.0);
    float bass = readState(1.0);
    float level = readState(2.0);
    float hue = color + readState(3.0);
    float turn = 6.2831853 * readState(4.0);

    // phase 0 and phase 1 show the same picture, so the wrap is invisible.
    float halfHeight = BASE_HALF_HEIGHT / zoom * exp(-phase * LOG_RHO);
    float angle = turn - phase * ARG_RHO;
    vec2 uv = (gl_FragCoord.xy - 0.5 * RENDERSIZE) / (0.5 * RENDERSIZE.y);
    vec2 dc = cmul(uv * halfHeight, vec2(cos(angle), sin(angle)));

    int limit = 64 + int(detail * 448.0);
    vec2 dz = vec2(0.0);
    vec2 derivative = vec2(0.0);
    vec2 z = vec2(0.0);
    int ref = 0;
    float n = 0.0;
    float m = 0.0;
    bool escaped = false;
    vec2 reference = vec2(0.0);
    // The first PREPERIOD steps follow the reference orbit onto its repelling fixed point z*.
    // They are iterated exactly; no pixel can escape or rebase this early.
    for (int i = 0; i < PREPERIOD; i++) {
        derivative = 2.0 * cmul(z, derivative) + vec2(1.0, 0.0);
        dz = 2.0 * cmul(reference, dz) + cmul(dz, dz) + dc;
        ref = i + 1;
        reference = orbit(ref);
        z = reference + dz;
    }
    // Near z* the offset grows linearly, dz' = lambda dz + dc with lambda = 2 z*, for dozens of
    // steps before it becomes large. Jump those steps in closed form: with A = dc / (lambda - 1),
    // dz_k = lambda^k (dz + A) - A, and the derivative likewise with B = 1 / (lambda - 1). Stop
    // while |dz| is still 1e-4 of |z*|, so the dropped dz^2 term stays negligible.
    vec2 lambda = 2.0 * reference;
    vec2 A = cdiv(dc, lambda - vec2(1.0, 0.0));
    vec2 B = cdiv(vec2(1.0, 0.0), lambda - vec2(1.0, 0.0));
    float k = max(floor(log(1e-4 * length(reference) / max(length(dz + A), 1e-30)) / LOG_RHO), 0.0);
    float turnK = k * atan(lambda.y, lambda.x);
    vec2 lambdaK = exp(k * LOG_RHO) * vec2(cos(turnK), sin(turnK));
    dz = cmul(lambdaK, dz + A) - A;
    derivative = cmul(lambdaK, derivative + B) - B;
    z = reference + dz;
    int start = PREPERIOD + int(k);
    for (int j = 0; j < 512; j++) {
        int i = start + j;
        if (i >= limit) break;
        derivative = 2.0 * cmul(z, derivative) + vec2(1.0, 0.0);
        dz = 2.0 * cmul(reference, dz) + cmul(dz, dz) + dc;
        ref = min(ref + 1, PREPERIOD);
        reference = orbit(ref);
        z = reference + dz;
        m = dot(z, z);
        if (m > 1e4) { escaped = true; n = float(i); break; }
        // Rebase onto the start of the reference orbit when the pixel's orbit passes
        // closer to zero than its offset; this keeps the perturbation accurate.
        if (m < dot(dz, dz)) { dz = z; ref = 0; reference = vec2(0.0); }
    }

    vec3 col;
    if (escaped) {
        // Subtract the loop's iteration shift so colors match across the wrap.
        float smoothCount = n + 1.0 - log2(0.5 * log2(m)) - phase;
        float distance = 0.5 * sqrt(m / max(dot(derivative, derivative), 1e-30)) * log(m);
        float edge = distance / (2.0 * halfHeight / RENDERSIZE.y);
        vec3 base = palette(hue + 0.035 * smoothCount);
        float bands = 0.55 + 0.45 * cos(6.2831853 * smoothCount * 0.25);
        col = base * bands * (0.15 + 0.85 * smoothstep(4.0, 30.0, smoothCount));
        col += mix(vec3(1.0), base, 0.4) * exp(-edge * 0.35) * (0.6 + 1.2 * level);
    } else {
        col = palette(hue + 0.5) * (0.02 + 0.06 * level);
    }
    gl_FragColor = vec4(col, 1.0);
}
