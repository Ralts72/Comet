#version 450
layout(constant_id = 0) const bool enabled = true;
layout(constant_id = 1) const float red = 1.0;
layout(constant_id = 2) const int green = 0;
layout(constant_id = 3) const uint blue = 0;
layout(location = 0) out vec4 color;
void main() {
    color = vec4(red, float(green), float(blue), 1);
    if (!enabled) {
        color = vec4(0, 0, 0, 1);
    }
}
