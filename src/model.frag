// The model, one material at a time, cut by the section plane: everything on
// its far side (dot(normal, p) > w) is discarded.
void MAIN()
{
    if (sectionEnabled > 0.5 && dot(VAR_WORLD_POSITION, sectionPlane.xyz) > sectionPlane.w)
        discard;
    BASE_COLOR = baseColor;
    ROUGHNESS = 0.85;
    METALNESS = 0.0;
    SPECULAR_AMOUNT = 0.2;
}
