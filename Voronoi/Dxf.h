#pragma once

#include <ostream>
#include <utility>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

void WriteDXFHeaders(std::ostream& out);
void BeginDXFSection(std::ostream& out);
void WriteDXFLine(std::ostream& out, const std::pair<glm::vec3, glm::vec3>& line);
void EndDXFSection(std::ostream& out);

