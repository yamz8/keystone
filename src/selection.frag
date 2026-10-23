void MAIN()
{
    if (sectionEnabled > 0.5 && dot(VAR_WORLD_POSITION, sectionPlane.xyz) > sectionPlane.w)
        discard;
    BASE_COLOR = vec4(accent.rgb, 1.0);
    EMISSIVE_COLOR = accent.rgb * 0.25;
    ROUGHNESS = 0.6;
    METALNESS = 0.0;
}
