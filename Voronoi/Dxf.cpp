#include "Dxf.h"

void WriteDXFHeaders(std::ostream& out)
{
	out <<
		"0\n"
		"SECTION\n"
			"2\n"
			"TABLES\n"
				"0\n"
				"TABLE\n"
					"2\n"
					"LAYER\n"
					"70\n"
					"6\n"
						"0\n"
						"LAYER\n"
						"2\n"
						"voronoi\n"
						"70\n"
						"64\n"
						"62\n"
						"7\n"
						"6\n"
						"CONTINUOUS\n"
				"0\n"
				"ENDTAB\n"
			"0\n"
			"TABLE\n"
				"2\n"
				"STYLE\n"
				"70\n"
				"0\n"
			"0\n"
			"ENDTAB\n"
		"0\n"
		"ENDSEC\n";
}

void BeginDXFSection(std::ostream& out)
{
	out <<
		"0\n"
		"SECTION\n"
		"2\n"
		"ENTITIES\n";
}

void WriteDXFLine(std::ostream& out, const std::pair<glm::vec3, glm::vec3>& line)
{
	out <<
		"0\n"
		"LINE\n"
		"8\n"
		"voronoi\n"
		"10\n" <<
		line.first.x << "\n"
		"20\n" <<
		line.first.y << "\n"
		"30\n" <<
		line.first.z << "\n"
		"11\n" <<
		line.second.x << "\n"
		"21\n" <<
		line.second.y << "\n"
		"31\n" <<
		line.second.z << "\n";
}

void EndDXFSection(std::ostream& out)
{
	out <<
		"0\n"
		"ENDSEC\n"
		"0\n"
		"EOF\n";
}