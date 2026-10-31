/*{"ISFVSN":"2.0","DESCRIPTION":"Original Sideshow drifting nebula of domain-warped noise with a glowing core and stars","CREDIT":"Sideshow contributors","CATEGORIES":["Generator","Fractal"],"INPUTS":[{"NAME":"spectrum","TYPE":"audioFFT","MAX":256},{"NAME":"speed","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":4.0},{"NAME":"zoom","TYPE":"float","DEFAULT":1.0,"MIN":0.5,"MAX":4.0},{"NAME":"color","TYPE":"float","DEFAULT":0.0,"MIN":0.0,"MAX":1.0},{"NAME":"detail","TYPE":"float","DEFAULT":0.4,"MIN":0.0,"MAX":1.0},{"NAME":"density","TYPE":"float","DEFAULT":0.5,"MIN":0.0,"MAX":1.0},{"NAME":"reactivity","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":3.0}],"PASSES":[{"TARGET":"state","PERSISTENT":true,"WIDTH":4,"HEIGHT":1},{}]}*/
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

// Value noise and fractal Brownian motion: octaves of noise, each twice as fine and half as
// strong, give the gas its self-similar billows.
float hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float fbm(vec2 p, int octaves) {
    float sum = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 6; i++) {
        if (i >= octaves) break;
        sum += amplitude * noise(p);
        // Rotate each octave so the grid never lines up.
        p = mat2(1.6, 1.2, -1.2, 1.6) * p;
        amplitude *= 0.5;
    }
    return sum;
}

void main() {
    if (PASSINDEX == 0) { gl_FragColor = updateState(0.004); return; }

    float phase = readState(0.0);
    float bass = readState(1.0);
    float level = readState(2.0);
    float hue = color + readState(3.0);
    float angle = 6.2831853 * phase;

    vec2 uv = (gl_FragCoord.xy - 0.5 * RENDERSIZE) / (0.5 * RENDERSIZE.y) / zoom;
    int octaves = 3 + int(detail * 3.0 + 0.5);
    // The drift runs around a loop, so the cloud wanders forever without the noise coordinates
    // growing large enough to lose precision.
    vec2 drift = 3.0 * vec2(cos(angle), sin(angle));
    vec2 p = uv * 1.6 + drift;

    // Domain warping: the gas is noise sampled at coordinates bent by more noise, which gives the
    // filaments and curling folds. Bass strengthens the bend so the cloud churns on hits.
    float bend = 2.2 + 1.2 * bass;
    vec2 q = vec2(fbm(p, octaves), fbm(p + vec2(5.2, 1.3), octaves));
    vec2 r = vec2(fbm(p + bend * q + vec2(1.7, 9.2) + 0.3 * angle, octaves),
                  fbm(p + bend * q + vec2(8.3, 2.8) - 0.2 * angle, octaves));
    float gas = fbm(p + bend * r, octaves);

    // A bright core drifts near the centre; the gas glows brightest around it.
    vec2 corePosition = 0.35 * vec2(cos(angle * 3.0), sin(angle * 2.0));
    float core = exp(-3.0 * length(uv - corePosition));
    // A broad, slow mask carves the cloud into islands of gas with open space between them,
    // and the gas thins away from the core.
    float mask = smoothstep(0.35, 0.65, fbm(uv * 0.7 - 0.5 * drift + 2.0 * q, 3) + 0.25 * core + 0.25 * density - 0.12);
    float thickness = smoothstep(0.35 - 0.2 * density, 0.9, gas) * mask;
    // Dust lanes: where the warp field folds over, absorb the light.
    float dust = smoothstep(0.45, 0.75, length(r)) * density;

    vec3 emission = mix(palette(hue + 0.6 * q.x), palette(hue + 0.25 + 0.5 * r.y), clamp(gas * gas * 2.0, 0.0, 1.0));
    vec3 col = emission * thickness * (0.35 + 1.4 * core) * (1.0 - 0.75 * dust);
    col += mix(vec3(1.0, 0.92, 0.8), palette(hue + 0.1), 0.3) * core * core * (0.6 + 0.8 * bass) * thickness;
    col *= 1.3 + 0.8 * level;

    // Stars: one candidate per grid cell, most cells empty, gently twinkling, dimmed by the gas.
    vec2 cell = floor(gl_FragCoord.xy / 6.0);
    float star = hash(cell);
    if (star > 0.985) {
        vec2 centre = (cell + 0.25 + 0.5 * vec2(hash(cell + 3.1), hash(cell + 7.7))) * 6.0;
        float twinkle = 0.6 + 0.4 * sin(angle * 40.0 + star * 600.0);
        float size = (star - 0.985) * 60.0;
        col += vec3(0.9, 0.95, 1.0) * twinkle * size * exp(-dot(gl_FragCoord.xy - centre, gl_FragCoord.xy - centre) * 0.8)
             * (1.0 - 0.7 * thickness) * (0.7 + 0.8 * level);
    }
    gl_FragColor = vec4(sqrt(max(col, 0.0)), 1.0);
}
