/*{"ISFVSN":"2.0","DESCRIPTION":"Original Sideshow plasma generator","CREDIT":"Sideshow contributors","INPUTS":[{"NAME":"speed","TYPE":"float","DEFAULT":0.7,"MIN":0.0,"MAX":4.0},{"NAME":"tint","TYPE":"color","DEFAULT":[0.2,0.6,1.0,1.0]}]}*/
void main() {
    vec2 p = (isf_FragNormCoord - 0.5) * 6.0;
    float t = TIME * speed;
    float wave = sin(p.x + t) + sin(p.y - t * 0.7) + sin(length(p) * 2.0 - t);
    vec3 color = 0.5 + 0.5 * cos(wave + vec3(0.0, 2.1, 4.2));
    gl_FragColor = vec4(color * mix(vec3(1.0), tint.rgb, 0.65), tint.a);
}
