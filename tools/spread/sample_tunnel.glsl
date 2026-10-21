void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
    vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float r = length(p);
    float a = atan(p.y, p.x);
    
    float z = 1.0 / (r + 0.08);
    float u = a / 3.14159265;
    float v = z + iTime * 0.5;
    
    vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 1.0, 2.0) + (u * 4.0 + v * 2.0));
    col *= smoothstep(0.0, 0.4, r);
    fragColor = vec4(col, 1.0);
}
