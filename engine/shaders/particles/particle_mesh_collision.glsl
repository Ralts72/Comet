struct MeshNode { vec4 minimum, maximum; uvec4 links; };
struct MeshTriangle { vec4 positions[3]; uvec4 layers; };
layout(std430, set=0, binding=6) readonly buffer MeshNodes { MeshNode mesh_nodes[]; };
layout(std430, set=0, binding=7) readonly buffer MeshTriangles { MeshTriangle mesh_triangles[]; };

bool triangle_contains(vec3 point, MeshTriangle triangle, vec3 normal) {
    for(int edge = 0; edge < 3; ++edge) {
        vec3 a = triangle.positions[edge].xyz;
        vec3 b = triangle.positions[(edge + 1) % 3].xyz;
        if(dot(cross(b - a, point - a), normal) < -1e-6) return false;
    }
    return true;
}
bool edge_contact(vec3 origin, vec3 direction, vec3 a, vec3 b, float radius, out ColliderHit hit) {
    vec3 edge = b - a;
    float size = length(edge);
    if(size < 1e-7) {
        if(!intersect_sphere(origin - a, direction, radius, hit)) return false;
        hit.position += a;
        return true;
    }
    vec3 y = edge / size;
    vec3 axis = abs(y.x) < .8 ? vec3(1, 0, 0) : vec3(0, 0, 1);
    vec3 x = normalize(cross(y, axis)), z = cross(x, y);
    mat3 basis = mat3(x, y, z);
    vec3 center = (a + b) * .5;
    if(!intersect_capsule(transpose(basis) * (origin - center), transpose(basis) * direction,
        radius, size * .5, hit)) return false;
    hit.position = center + basis * hit.position;
    hit.normal = basis * hit.normal;
    return true;
}
bool triangle_contact(vec3 origin, vec3 direction, float radius, MeshTriangle triangle,
    out ColliderHit hit) {
    vec3 a = triangle.positions[0].xyz;
    vec3 normal = normalize(cross(triangle.positions[1].xyz - a, triangle.positions[2].xyz - a));
    float distance = dot(origin - a, normal);
    vec3 projected = origin - normal * distance;
    float side = distance < 0 ? -1 : 1;
    hit.fraction = 2;
    hit.inside = false;
    if(abs(distance) < radius && triangle_contains(projected, triangle, normal)) {
        hit.inside = true;
        hit.fraction = 0;
        hit.normal = normal * side;
        hit.position = projected + hit.normal * radius;
    } else {
        float velocity = dot(direction, normal);
        if(velocity * side < -1e-8) {
            float fraction = (side * radius - distance) / velocity;
            vec3 position = origin + direction * fraction;
            if(fraction >= 0 && fraction <= 1
                && triangle_contains(position - normal * side * radius, triangle, normal)) {
                hit.fraction = fraction;
                hit.normal = normal * side;
                hit.position = position;
            }
        }
    }
    for(int edge = 0; edge < 3; ++edge) {
        ColliderHit contact;
        if(edge_contact(origin, direction, triangle.positions[edge].xyz,
            triangle.positions[(edge + 1) % 3].xyz, radius, contact)
            && contact.fraction < hit.fraction) hit = contact;
    }
    return hit.fraction <= 1;
}
bool mesh_box_hit(MeshNode node, vec3 origin, vec3 direction, float radius, float maximum) {
    float first = 0, last = min(maximum, 1);
    for(int axis = 0; axis < 3; ++axis) {
        float low = node.minimum[axis] - radius, high = node.maximum[axis] + radius;
        if(abs(direction[axis]) < 1e-10) {
            if(origin[axis] < low || origin[axis] > high) return false;
        } else {
            float a = (low - origin[axis]) / direction[axis];
            float b = (high - origin[axis]) / direction[axis];
            first = max(first, min(a, b));
            last = min(last, max(a, b));
            if(first > last) return false;
        }
    }
    return true;
}
bool mesh_contact(vec3 origin, vec3 direction, float maximum, out ColliderHit hit, out bool exhausted) {
    uint index = 0, budget = world.w;
    float radius = max(timing.z, 1e-5);
    hit.fraction = maximum;
    exhausted = false;
    bool found = false;
    while(index < history.z) {
        MeshNode node = mesh_nodes[index];
        if((node.links.z & world.y) == 0) {
            index = node.links.w;
            continue;
        }
        if(budget == 0) { exhausted = true; return found; }
        --budget;
        if(!mesh_box_hit(node, origin, direction, radius, hit.fraction)) {
            index = node.links.w;
            continue;
        }
        ++index;
        for(uint item = 0; item < node.links.y; ++item) {
            if(budget == 0) { exhausted = true; return found; }
            --budget;
            MeshTriangle triangle = mesh_triangles[node.links.x + item];
            if((triangle.layers.x & world.y) == 0) continue;
            ColliderHit contact;
            if(triangle_contact(origin, direction, radius, triangle, contact)
                && contact.fraction < hit.fraction) {
                hit = contact;
                found = true;
            }
        }
    }
    return found;
}
