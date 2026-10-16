/*{"ISFVSN":"2.0","DESCRIPTION":"Original Sideshow persistent trails","CREDIT":"Sideshow contributors","INPUTS":[{"NAME":"reset","TYPE":"event"},{"NAME":"decay","TYPE":"float","DEFAULT":0.96,"MIN":0.0,"MAX":0.99}],"PASSES":[{"TARGET":"history","PERSISTENT":true},{}]}*/
void main() {
    if (PASSINDEX == 0) {
        vec2 p = isf_FragNormCoord;
        vec2 center = 0.5 + 0.28 * vec2(cos(TIME * 0.7), sin(TIME * 0.9));
        float spot = exp(-90.0 * dot(p - center, p - center));
        vec3 previous = reset ? vec3(0.0) : IMG_THIS_PIXEL(history).rgb;
        gl_FragColor = vec4(previous * decay + vec3(0.3, 0.8, 1.0) * spot * 0.08, 1.0);
    } else {
        gl_FragColor = IMG_THIS_PIXEL(history);
    }
}
