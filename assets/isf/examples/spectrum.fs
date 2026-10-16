/*{"ISFVSN":"2.0","DESCRIPTION":"Original Sideshow microphone spectrum","CREDIT":"Sideshow contributors","INPUTS":[{"NAME":"spectrum","TYPE":"audioFFT","MAX":512},{"NAME":"gain","TYPE":"float","DEFAULT":2.0,"MIN":0.0,"MAX":10.0}]}*/
void main() {
    vec2 p = isf_FragNormCoord;
    float bin = floor(p.x * 64.0) * 4.0;
    float level = max(IMG_PIXEL(spectrum, vec2(bin + 0.5, 0.5)).r,
                      IMG_PIXEL(spectrum, vec2(bin + 0.5, 1.5)).r);
    float bar = step(p.y, clamp(level * gain, 0.0, 1.0));
    vec3 ink = mix(vec3(0.1, 0.8, 1.0), vec3(1.0, 0.2, 0.4), p.y);
    gl_FragColor = vec4(vec3(0.008, 0.012, 0.02) + ink * bar, 1.0);
}
