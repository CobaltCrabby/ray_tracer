#include <vk_mesh.h>
#include <tiny_obj_loader.h>
#include <iostream>

VertexInputDescription Vertex::get_vertex_description() {
    VertexInputDescription description;

	VkVertexInputBindingDescription mainBinding = {};
	mainBinding.binding = 0;
	mainBinding.stride = sizeof(Vertex);
	mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	description.bindings.push_back(mainBinding);

	VkVertexInputAttributeDescription positionAttribute = {};
	positionAttribute.binding = 0;
	positionAttribute.location = 0;
	positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	positionAttribute.offset = offsetof(Vertex, position);

	VkVertexInputAttributeDescription normalAttribute = {};
	normalAttribute.binding = 0;
	normalAttribute.location = 1;
	normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	normalAttribute.offset = offsetof(Vertex, normal);

	VkVertexInputAttributeDescription colorAttribute = {};
	colorAttribute.binding = 0;
	colorAttribute.location = 2;
	colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	colorAttribute.offset = offsetof(Vertex, color);

	VkVertexInputAttributeDescription uvAttribute = {};
	uvAttribute.binding = 0;
	uvAttribute.location = 3;
	uvAttribute.format = VK_FORMAT_R32G32_SFLOAT;
	uvAttribute.offset = offsetof(Vertex, uv);

	description.attributes.push_back(positionAttribute);
	description.attributes.push_back(normalAttribute);
	description.attributes.push_back(colorAttribute);
	description.attributes.push_back(uvAttribute);
	return description;
}

bool Mesh::load_from_obj(char* filePath) {
    tinyobj::attrib_t attribute;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warning;
    std::string error;

    tinyobj::LoadObj(&attribute, &shapes, &materials, &warning, &error, filePath, nullptr);

    if (!warning.empty()) {
        std::cout << warning << std::endl;
    }

    if (!error.empty()) {
        std::cout << error << std::endl;
        return false;
    }

	for (size_t s = 0; s < shapes.size(); s++) {
		size_t index_offset = 0;
		for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {

			int fv = 3;

			for (size_t v = 0; v < fv; v++) {
				// access to vertex
				tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

                // vertex position
				tinyobj::real_t vx = attribute.vertices[3 * idx.vertex_index + 0];
				tinyobj::real_t vy = attribute.vertices[3 * idx.vertex_index + 1];
				tinyobj::real_t vz = attribute.vertices[3 * idx.vertex_index + 2];
                // vertex normal
            	tinyobj::real_t nx = attribute.normals[3 * idx.normal_index + 0];
				tinyobj::real_t ny = attribute.normals[3 * idx.normal_index + 1];
				tinyobj::real_t nz = attribute.normals[3 * idx.normal_index + 2];
				// vertex uv
				tinyobj::real_t ux = attribute.texcoords[2 * idx.texcoord_index + 0];
				tinyobj::real_t uy = attribute.texcoords[2 * idx.texcoord_index + 1];

                // copy it into our vertex
				Vertex new_vert;
				new_vert.position.x = vx;
				new_vert.position.y = vy;
				new_vert.position.z = vz;

				new_vert.normal.x = nx;
				new_vert.normal.y = ny;
                new_vert.normal.z = nz;

				new_vert.uv.x = ux;
				new_vert.uv.y = 1 - uy;

                // we are setting the vertex color as the vertex normal. This is just for display purposes
                new_vert.color = new_vert.normal;

				vertices.push_back(new_vert);
			}
			index_offset += fv;
		}
	}

    return true;
}