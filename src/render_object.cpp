#include "render_object.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

void TLAS::readObj(const char* filePath) {
    std::ifstream fileStream;
    fileStream.open(filePath);
    if (!fileStream.is_open()) {
        throw std::runtime_error("failed to load an obj file: " + std::string(filePath));
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> normals;

    uint start = triangles.size();

    while (fileStream) {
        std::string fileLine;
        std::getline(fileStream, fileLine);

        std::string prefix = fileLine.substr(0, fileLine.find(' '));

        if (prefix == "v") { // POSITIONS
            int index = 2;
			glm::vec3 position;
			for (int i = 0; i < 3; i++) {
				int nextSpace = fileLine.find(' ', index);
				position[i] = stof(fileLine.substr(index, nextSpace - index));
				index = nextSpace + 1;
			}
			positions.push_back(position);
        } else if (prefix == "vt") { // UV COORDINATES
			int firstSpace = fileLine.find(' ', 2);
			int secondSpace = fileLine.find(' ', firstSpace + 1);
			float u = stof(fileLine.substr(firstSpace + 1, secondSpace - firstSpace - 1));
			float v = stof(fileLine.substr(secondSpace, fileLine.length() - secondSpace));
			uvs.push_back({u, v});
		} else if (prefix == "vn") { // NORMAL VECTORS
			uint index = 3;
			glm::vec3 normal;
			for (int i = 0; i < 3; i++) {
				int nextSpace = fileLine.find(' ', index);
				normal[i] = stof(fileLine.substr(index, nextSpace - index));
				index = nextSpace + 1;
			}
			normals.push_back(normal);
		} else if (prefix == "f") { // FACES (ONLY TRIANGLES FOR NOW)
            uint index = 0;
            uint pointIndex[3];
            glm::vec3 centroid;
            
			for (int i = 0; i < 3; i++) {
				int space = fileLine.find(' ', index);
				int nextSpace = fileLine.find(' ', space + 1);
				std::string vertex = fileLine.substr(space + 1, nextSpace - space - (i == 2 ? 0 : 1));

				int firstSlash = vertex.find('/');
				int secondSlash = vertex.find('/', firstSlash + 1);

				std::string vIndexStr = vertex.substr(0, firstSlash);
                uint vertexInd = 0;
				if (!vIndexStr.empty()) {
					vertexInd = stoi(vIndexStr) - 1;
				}

				std::string uvIndexStr = vertex.substr(firstSlash + 1, secondSlash - firstSlash - 1);
                uint textureInd = 0;
				if (!uvIndexStr.empty()) {
					textureInd = stoi(uvIndexStr) - 1;
				}

				std::string nIndexStr = vertex.substr(secondSlash + 1, vertex.size() - secondSlash - 1);
                uint normalInd = 0;
				if (!nIndexStr.empty()) {
					normalInd = stoi(nIndexStr) - 1;
				}
                
                pointIndex[i] = vertices.size();

                glm::vec3 pos = positions[vertexInd];
                glm::vec2 uv = uvs[textureInd];
                glm::vec3 normal = normals[normalInd];
                centroid += pos;
                vertices.push_back({glm::vec4(pos, uv.x), glm::vec4(normal, uv.y)});

				index = nextSpace;
			} 

            centroids.push_back(centroid / 3.f);
            triangles.push_back({pointIndex[0], pointIndex[1], pointIndex[2]});
		} 
    }
    buildBVH(start);
}

void TLAS::updateBVHBounds(uint index) {
	BVHNode& node = bvhNodes[index];
	BoundingBox box;

	for (int i = 0; i < node.triCount; i++) {
		Triangle& leafTri = triangles[node.index + i];
		box.grow(vertices[leafTri.v1]);
		box.grow(vertices[leafTri.v2]);
		box.grow(vertices[leafTri.v3]);
	}

	node.boundsX = glm::vec2(box.bounds[0].x, box.bounds[1].x);
	node.boundsY = glm::vec2(box.bounds[0].y, box.bounds[1].y);
	node.boundsZ = glm::vec2(box.bounds[0].z, box.bounds[1].z);
}

void TLAS::buildBVH(uint start) {
	auto begin = std::chrono::system_clock::now();
    uint size = triangles.size() - start;

	nodesUsed++;
	uint offset = bvhNodes.size();
	bvhNodes.resize(bvhNodes.size() + (size * 2 - 1));
	BVHNode& root = bvhNodes[offset];
	root.index = start;
	root.triCount = size;

	BVHStats stats;

	updateBVHBounds(offset);
	subdivideBVH(offset, 0, stats);

	bvhNodes.resize(nodesUsed);
	bvhNodes.shrink_to_fit();	

	auto end = std::chrono::system_clock::now();    
	auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
	std::cout << "BVH Build Time: " << time.count() << "ms\n";
	std::cout << "Node Count: " << nodesUsed - offset << std::endl;
	std::cout << "Max Depth: " << stats.maxDepth << std::endl;
	std::cout << "Min Depth: " << stats.minDepth << std::endl;
	std::cout << "Max Tris: " << stats.maxTri << std::endl;
}

void TLAS::subdivideBVH(uint index, uint depth, BVHStats& stats) {
	BVHNode& node = bvhNodes[index];
	
	if (node.triCount <= 2 || depth >= 64) {
		stats.maxDepth = iMax(depth, stats.maxDepth);
		stats.minDepth = iMin(depth, stats.minDepth);
		stats.maxTri = iMax(node.triCount, stats.maxTri);
		return;
	}

	int axis = 0;
	float splitPos = 0.f;
	float bestCost = findBVHSplitPlane(node, axis, splitPos);	

    BoundingBox nodeBounds = {
        glm::vec4(node.boundsX[0], node.boundsY[0], node.boundsZ[0], 0.f), 
        glm::vec4(node.boundsX[1], node.boundsY[1], node.boundsZ[1], 0.f)
    };

    float noSplitCost = node.triCount * nodeBounds.surfaceArea();
	if (bestCost >= noSplitCost) {
		stats.maxDepth = iMax(depth, stats.maxDepth);
		stats.minDepth = iMin(depth, stats.minDepth);
		stats.maxTri = iMax(node.triCount, stats.maxTri);
		return;
	}

	// partition the triangles
	int i = node.index;
	int j = i + node.triCount - 1;
	while (i <= j) {
		glm::vec3 centroid = centroids[i];

		// swap so left side of array is less than splitPos
		if (centroid[axis] < splitPos) {
			i++;
		} else {
			std::swap(triangles[i], triangles[j]);
			std::swap(centroids[i], centroids[j]);
			j--;
		}
	}

	// if one side has all tris, abort
	int triIndex = node.index;
	int leftCount = i - triIndex;
	if (leftCount == 0 || leftCount == node.triCount) {
		stats.maxDepth = iMax(depth, stats.maxDepth);
		stats.minDepth = iMin(depth, stats.minDepth);
		stats.maxTri = iMax(node.triCount, stats.maxTri);
		return;
	}

	// node.index is always at first a tri ifor (int index, only becomes a node index after a split
	node.index = nodesUsed;
	nodesUsed += 2; // right node increase
	bvhNodes[node.index].index = triIndex;
	bvhNodes[node.index].triCount = leftCount;
	bvhNodes[node.index + 1].index = i;
	bvhNodes[node.index + 1].triCount = node.triCount - leftCount;

	node.triCount = 0;
	updateBVHBounds(node.index);
	updateBVHBounds(node.index + 1);

	subdivideBVH(node.index, depth + 1, stats);
	subdivideBVH(node.index + 1, depth + 1, stats);
}

float TLAS::findBVHSplitPlane(BVHNode& node, int& axis, float& splitPos) {
	float bestCost = 1e30f;
	for (int a = 0; a < 3; a++) {
		float min = 1e30f;
		float max = -1e30f;
		for (int i = 0; i < node.triCount; i++) {
			min = iMin(min, centroids[node.index + i][a]);
			max = iMax(max, centroids[node.index + i][a]);
		}

		if (min == max) continue;

		//populate bins
		BVHBin bins[BINS];
		float scale = BINS / (max - min);
		for (int i = 0; i < node.triCount; i++) {
			Triangle tri = triangles[node.index + i];
			int binIndex = iMin(BINS - 1, floor((centroids[node.index + i][a] - min) * scale));
			bins[binIndex].triCount++;
			bins[binIndex].box.grow(vertices[tri.v1]);
			bins[binIndex].box.grow(vertices[tri.v2]);
			bins[binIndex].box.grow(vertices[tri.v3]);
		}

		//data for planes between the bins, loop through to find each
		float leftArea[BINS - 1];
		float rightArea[BINS - 1];
		float leftCount[BINS - 1];
		float rightCount[BINS - 1];
		BoundingBox leftBox;
		BoundingBox rightBox;
		int leftSum = 0;
		int rightSum = 0;
		
		for (int i = 0; i < BINS - 1; i++) {
			leftSum += bins[i].triCount;
			leftCount[i] = leftSum;
			leftBox.grow(bins[i].box);
			leftArea[i] = leftBox.surfaceArea();

			rightSum += bins[BINS - 1 - i].triCount;
			rightCount[BINS - 2 - i] = rightSum;
			rightBox.grow(bins[BINS - 1 - i].box);
			rightArea[i] = rightBox.surfaceArea();
			rightArea[BINS - 2 - i] = rightBox.surfaceArea();
		}

		scale = (max - min) / BINS;
		for (int i = 0; i < BINS - 1; i++) {
			float cost = leftCount[i] * leftArea[i] + rightCount[i] * rightArea[i];
			if (cost < bestCost) {
				axis = a;
				splitPos = min + scale * (i + 1);
				bestCost = cost;
			}
		}
	}

	return bestCost;
}