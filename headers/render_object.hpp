#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>
#include <unordered_map>
#include <vulkan/vulkan_core.h>
#include <vector>

// inlining these makes it faster ive read, not sure why
inline float iMax(float a, float b) {
	return a < b ? b : a;
} 

inline float iMin(float a, float b) {
	return a < b ? a : b;
} 

const uint BINS = 20;

class Sphere {
    public:
        float radius;
        glm::vec3 p;
        uint materialIndex;

        Sphere(float r, glm::vec3 p, uint m);
};

class BLAS {
    public:
        struct Vertex {
            // compacted for memory reasons
            glm::vec4 position; // position.w = uv.x
            glm::vec4 normal; // normal.w = uv.y
        };
        
        struct Triangle {
            uint v1, v2, v3;
        };
        
        struct BoundingBox {
            glm::vec4 bounds[2] = {glm::vec4(1e30f), glm::vec4(-1e30f)};
        
            void grow(BoundingBox box) {
                bounds[0] = glm::min(bounds[0], box.bounds[0]);
                bounds[1] = glm::max(bounds[1], box.bounds[1]);
            }

            void grow(Vertex v0) {
                for (int i = 0; i < 3; i++) {
                    bounds[0][i] = iMin(v0.position[i], bounds[0][i]);
                    bounds[1][i] = iMax(v0.position[i], bounds[1][i]);
                }
            }
        
            void grow(glm::vec3 v0) {
                for (int i = 0; i < 3; i++) {
                    bounds[0][i] = iMin(v0[i], bounds[0][i]);
                    bounds[1][i] = iMax(v0[i], bounds[1][i]);
                }
            }
        
            float surfaceArea() {
                float x = bounds[1].x - bounds[0].x;
                float y = bounds[1].y - bounds[0].y;
                float z = bounds[1].z - bounds[0].z;
                return x * y + y * z + z * x;
            }
        };

        struct BVHNode {
            glm::vec2 boundsX;
            glm::vec2 boundsY;
            glm::vec2 boundsZ;
            uint index = 0;
            uint triCount = 0;
            //if triCount == 0: index is a node index, else: index is a triangle index
        };

        struct BVHBin {
            BoundingBox box;
            uint triCount = 0;
        };

        struct BVHStats {
            uint minDepth = 4294967295;
            uint maxDepth = 0;
            uint maxTri = 0;
        };

        std::vector<BVHNode> bvhNodes;
        std::vector<Triangle> triangles;
        std::vector<Vertex> vertices;
        std::vector<glm::vec3> centroids;
        std::unordered_map<const char*, uint> createdObjects;
        uint nodesUsed = 0;

        void readObj(const char* filePath);

        void buildBVH(uint start);
        void updateBVHBounds(uint index);
        void subdivideBVH(uint index, uint depth, BVHStats& stats);
        float findBVHSplitPlane(BVHNode& node, int& axis, float& splitPos);

        void buildBLAS();
        BLAS() = default;
    };
