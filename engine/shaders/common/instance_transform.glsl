#ifndef COMET_INSTANCE_TRANSFORM_GLSL
#define COMET_INSTANCE_TRANSFORM_GLSL

layout(location = 3) in vec4 instance_column0;
layout(location = 4) in vec4 instance_column1;
layout(location = 5) in vec4 instance_column2;
layout(location = 6) in vec4 instance_column3;

mat4 instance_transform() {
    return mat4(instance_column0, instance_column1, instance_column2, instance_column3);
}

#endif
