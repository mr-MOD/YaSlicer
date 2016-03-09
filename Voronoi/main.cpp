#include "Voronoi.h"
#include "Dxf.h"

#include <ErrorHandling.h>

#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

struct Graph
{
	using VertexList = std::vector<glm::vec3>;
	using Edge = std::pair<size_t, size_t>;
	using EdgeList = std::vector<Edge>;
	Graph(const EdgeList& eList, const VertexList& vList) : edgeList(eList), vertexList(vList) {}

	EdgeList edgeList;
	VertexList vertexList;
};

void WriteDXFFile(const Settings& settings, const Graph& g)
{
	std::fstream dxf(settings.outputDXFFile, std::ios::out);
	WriteDXFHeaders(dxf);
	BeginDXFSection(dxf);

	for (const auto& seg : g.edgeList)
	{
		WriteDXFLine(dxf, std::make_pair(g.vertexList[seg.first], g.vertexList[seg.second]));
	}
	EndDXFSection(dxf);
	dxf.close();
}

Graph BuildGraph(const std::vector<Segment>& segments)
{
	class Vec3Hasher
	{
	public:
		size_t operator()(const glm::vec3 &v) const
		{
			const auto Precision = 0.01f;
			std::size_t seed = 0;
			boost::hash_combine(seed, int64_t(v.x / Precision + 0.5f) * Precision);
			boost::hash_combine(seed, int64_t(v.y / Precision + 0.5f) * Precision);
			boost::hash_combine(seed, int64_t(v.z / Precision + 0.5f) * Precision);
			return seed;
		}
	};
	std::unordered_map<glm::vec3, size_t, Vec3Hasher> vertexMerger;

	Graph::EdgeList edgeList;
	edgeList.reserve(segments.size());
	for (const auto& s : segments)
	{
		auto itStart = vertexMerger.insert(std::make_pair(s.first, vertexMerger.size()));
		auto itEnd = vertexMerger.insert(std::make_pair(s.second, vertexMerger.size()));

		edgeList.push_back(std::make_pair(itStart.first->second, itEnd.first->second));
	}

	Graph::VertexList vertexList;
	vertexList.resize(vertexMerger.size());
	for (const auto& v : vertexMerger)
	{
		vertexList[v.second] = v.first;
	}
	return Graph(edgeList, vertexList);
}

void CollapseEdges(Graph& g, const Settings& settings)
{
	std::vector<uint32_t> vertexUseCount(g.vertexList.size(), 0);
	for (const auto& edge : g.edgeList)
	{
		++vertexUseCount[edge.first];
		++vertexUseCount[edge.second];
	}

	std::unordered_set<size_t> endVertices;
	for (size_t i = 0; i < vertexUseCount.size(); ++i)
	{
		if (vertexUseCount[i] == 1)
		{
			endVertices.insert(i);
		}
	}

	while (true)
	{
		auto smallEdgeIt = std::find_if(g.edgeList.begin(), g.edgeList.end(), [&g, &settings, &endVertices](const Graph::Edge& e) {
			return endVertices.count(e.first) == 0 && endVertices.count(e.second) == 0 &&
				glm::distance(g.vertexList[e.first], g.vertexList[e.second]) < settings.minEdgeSize;
		});
		
		if (smallEdgeIt != g.edgeList.end())
		{
			g.vertexList[smallEdgeIt->first] =
				(g.vertexList[smallEdgeIt->first] + g.vertexList[smallEdgeIt->second]) * 0.5f;

			const auto removeVertex = smallEdgeIt->second;
			for (auto& e : g.edgeList)
			{
				if (e.first == removeVertex)
				{
					e.first = smallEdgeIt->first;
				}
				else if (e.second == removeVertex)
				{
					e.second = smallEdgeIt->first;
				}
			}

			std::swap(g.edgeList.back(), *smallEdgeIt);
			g.edgeList.pop_back();
		}
		else
		{
			break;
		}
	}
}

void EnlargePeripheralEdges(Graph& g, const Settings& settings)
{
	std::vector<uint32_t> vertexUseCount(g.vertexList.size(), 0);
	for (const auto& edge : g.edgeList)
	{
		++vertexUseCount[edge.first];
		++vertexUseCount[edge.second];
	}

	for (auto& e : g.edgeList)
	{
		if (vertexUseCount[e.first] == 1)
		{
			std::swap(e.first, e.second);
		}

		if (vertexUseCount[e.second] == 1)
		{
			glm::vec3 direction = glm::normalize(g.vertexList[e.second] - g.vertexList[e.first]);
			g.vertexList[e.second] += direction * settings.extEdgesAddLength;
		}
	}
}

#include "PlanarParam.h"
#include "Loaders.h"
int main(int argc, char** argv)
{
	/*std::vector<float> vb;
	std::vector<uint32_t> ib;
	LoadStl("test.stl", vb, ib);
	PlanarParametrize(vb, ib);
	return 0;*/

	Settings settings;
	namespace po = boost::program_options;
	
	po::options_description config("options");
	config.add_options()
		("help,h", "produce help message")
		("modelFile,m", po::value<std::string>(&settings.modelFile), "model to process (STL)")
		("cellSize", po::value<float>(&settings.cellSize)->default_value(3.0f), "cell size (mm)")
		("facetSize", po::value<float>(&settings.facetSize)->default_value(3.0f), "facet size (mm)")
		("facetMaxDistance", po::value<float>(&settings.facetDistance)->default_value(0.5f), "remeshing criteria, max deviation from original surface (mm)")
		("minEdgeSize", po::value<float>(&settings.minEdgeSize)->default_value(1.0f), "minimum edge length (mm)")
		("extEdgesAddLength,l", po::value<float>(&settings.extEdgesAddLength)->default_value(0.1f), "additional length for exterior edges (mm)")
		("genSticks,s", po::value<bool>(&settings.generateSticks)->default_value(true), "generate sticks to surface")
		//("generateMesh", po::value<bool>(&settings.shouldGenerateMesh)->default_value(false), "generate stl file")
		;

	po::options_description cmdline_options;
	cmdline_options.add(config);

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, cmdline_options), vm);
	po::notify(vm);

	if (vm.count("help") || argc < 2)
	{
		std::cout << "Voronoi generator v0.11, 2016" << "\n";
		std::cout << cmdline_options << "\n";
		return 0;
	}

	if (settings.modelFile.empty())
	{
		std::cout << "Need STL file for processing" << "\n";
		return 0;
	}

	settings.outputDXFFile = boost::filesystem::path(settings.modelFile).replace_extension("dxf").string();
	settings.outputMeshFile = boost::filesystem::path(settings.modelFile).replace_extension("stl").string();
	
	auto edges = GenerateVoronoiEdges(settings);
	auto graph = BuildGraph(edges);
	CollapseEdges(graph, settings);
	EnlargePeripheralEdges(graph, settings);
	WriteDXFFile(settings, graph);

	return 0;
}