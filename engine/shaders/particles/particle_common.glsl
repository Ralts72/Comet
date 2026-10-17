struct Spawn { mat4 pose; vec4 timing; uvec4 range; };
struct Particle { vec4 position_size; vec4 color; vec4 uv_rect; vec4 sort_key; };
struct Parameters {
    mat4 view;
    vec4 acceleration_lifetime, start_color, end_color, sizes_speed_cone, atlas, animation;
    uvec4 counts;
};
uint random_hash(uint value) {
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
float random_unit(uint value) { return float(random_hash(value) >> 8) / 16777216.0; }
Particle empty_particle(uint index) {
    return Particle(vec4(0),vec4(0),vec4(0,0,1,1),vec4(3.402823e38,float(index),0,-1));
}
vec3 birth_velocity(Spawn birth, uint local, Parameters parameters) {
    uint seed=random_hash(birth.range.z+local)^parameters.counts.w;
    float y=mix(parameters.sizes_speed_cone.w,1.0,random_unit(seed));
    float radius=sqrt(max(0.0,1.0-y*y)), angle=6.28318530718*random_unit(seed+1u);
    return mat3(birth.pose)*vec3(radius*cos(angle),y,radius*sin(angle))*parameters.sizes_speed_cone.z;
}
Particle particle_appearance(vec3 position, float age, uint index, Parameters parameters) {
    Particle particle=empty_particle(index);
    float phase=clamp(age/parameters.acceleration_lifetime.w,0.0,1.0);
    particle.position_size=vec4(position,mix(parameters.sizes_speed_cone.x,parameters.sizes_speed_cone.y,phase));
    particle.color=mix(parameters.start_color,parameters.end_color,phase);
    if(age>=parameters.acceleration_lifetime.w) particle.color.a=0;
    float frame=floor(parameters.animation.x>0.0 ? age*parameters.animation.x : phase*parameters.atlas.z);
    frame=parameters.animation.y>.5 && parameters.animation.x>0.0
        ? mod(frame,parameters.atlas.z) : min(frame,parameters.atlas.z-1.0);
    particle.uv_rect=vec4(mod(frame,parameters.atlas.x)/parameters.atlas.x,
        floor(frame/parameters.atlas.x)/parameters.atlas.y,1.0/parameters.atlas.xy);
    particle.sort_key.x=(parameters.view*vec4(position,1)).z;
    return particle;
}
