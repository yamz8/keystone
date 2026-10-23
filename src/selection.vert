// The selected element, drawn over itself: pulled a hair towards the camera
// so it wins the depth test against the same triangles in their own material,
// without rebuilding the model to leave them out.
void MAIN()
{
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);
    POSITION.z -= 1e-5 * POSITION.w;
}
