/*{"ISFVSN":"2.0","DESCRIPTION":"Original Sideshow stained-glass Newton fractal whose four roots wander and braid the basins","CREDIT":"Sideshow contributors","CATEGORIES":["Generator","Fractal"],"INPUTS":[{"NAME":"spectrum","TYPE":"audioFFT","MAX":256},{"NAME":"speed","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":4.0},{"NAME":"zoom","TYPE":"float","DEFAULT":1.0,"MIN":0.5,"MAX":4.0},{"NAME":"color","TYPE":"float","DEFAULT":0.0,"MIN":0.0,"MAX":1.0},{"NAME":"detail","TYPE":"float","DEFAULT":0.4,"MIN":0.0,"MAX":1.0},{"NAME":"reactivity","TYPE":"float","DEFAULT":1.0,"MIN":0.0,"MAX":3.0}],"PASSES":[{"TARGET":"state","PERSISTENT":true,"WIDTH":4,"HEIGHT":1},{}]}*/
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
    if (PASSINDEX == 0) { gl_FragColor = updateState(0.01); return; }

    float phase = readState(0.0);
    float bass = readState(1.0);
    float level = readState(2.0);
    float hue = color + readState(3.0);
    float angle = 6.2831853 * phase;

    // Newton's method for a polynomial with four roots that wander on their own orbits. Each
    // point is coloured by the root it falls into, so the basins slide and braid as the roots
    // move. Bass over-relaxes the step (a > 1), which shatters the boundaries.
    vec2 roots[4];
    for (int k = 0; k < 4; k++) {
        float fk = float(k);
        float a = angle * (fk < 1.5 ? 1.0 : -1.0) * (1.0 + 0.5 * mod(fk, 2.0)) + fk * 1.5707963;
        roots[k] = vec2(cos(a), sin(a)) * (0.75 + 0.35 * sin(angle * 2.0 + fk * 2.1));
    }
    float relax = 1.0 + 0.45 * bass;

    float halfHeight = 1.6 / zoom;
    vec2 z = (gl_FragCoord.xy - 0.5 * RENDERSIZE) / (0.5 * RENDERSIZE.y) * halfHeight;
    int limit = 16 + int(detail * 80.0);
    float n = float(limit);
    float root = -1.0;
    float closest = 1.0;
    vec2 dz = vec2(1.0, 0.0);
    for (int i = 0; i < 96; i++) {
        if (i >= limit) break;
        // p'/p = sum 1/(z - r_k), so the Newton step is 1 / that sum.
        // Track d(z)/d(pixel) too: d/dz (z - a / S) = 1 - a T / S^2 with T = sum 1/(z - r_k)^2.
        vec2 sum = vec2(0.0);
        vec2 sum2 = vec2(0.0);
        for (int k = 0; k < 4; k++) {
            vec2 inverse = cdiv(vec2(1.0, 0.0), z - roots[k]);
            sum += inverse;
            sum2 += cmul(inverse, inverse);
        }
        vec2 inverseSum = cdiv(vec2(1.0, 0.0), sum);
        dz = cmul(vec2(1.0, 0.0) - relax * cmul(sum2, cmul(inverseSum, inverseSum)), dz);
        z -= relax * inverseSum;
        for (int k = 0; k < 4; k++) {
            float d = dot(z - roots[k], z - roots[k]);
            if (d < 1e-4) { root = float(k); closest = d; break; }
        }
        if (root >= 0.0) { n = float(i); break; }
    }

    vec3 col;
    if (root < 0.0) {
        col = vec3(0.02 + 0.05 * level);
    } else {
        // Smooth count from how deep inside the capture radius the point landed.
        float smoothCount = n + clamp(log2(log(closest) / log(1e-4)), -1.0, 1.0) * -1.0;
        vec3 base = palette(hue + root * 0.25 + 0.015 * smoothCount);
        // Distance to the nearest basin boundary, |e| |log e| / |de| for the final error e.
        float distance = sqrt(closest / max(dot(dz, dz), 1e-30)) * -0.5 * log(closest);
        // Stained glass lit from behind: basins stay deep, colour brightens toward the tangled
        // boundaries, and thin pale leading marks each iteration band.
        float glow = exp(-distance / (0.06 * halfHeight));
        float lead = pow(abs(fract(smoothCount) - 0.5) * 2.0, 8.0);
        col = base * (0.1 + 0.9 * glow) * (1.0 - 0.6 * lead);
        col += vec3(1.0, 0.95, 0.9) * lead * glow * (0.15 + 0.5 * level);
    }
    gl_FragColor = vec4(sqrt(col * (0.9 + 0.35 * level)), 1.0);
}
