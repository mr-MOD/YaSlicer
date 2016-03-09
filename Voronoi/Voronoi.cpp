#include "Voronoi.h"
#include "Loaders.h"

#include <ErrorHandling.h>

#include <glm/ext.hpp>
#include <glm/gtx/normal.hpp>

#include <unordered_set>
#include <algorithm>
#include <memory>
#include <string>

#include <boost/functional/hash.hpp>
//#define CGAL_MESH_3_VERBOSE
// voronoi
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Mesh_triangulation_3.h>
#include <CGAL/Mesh_complex_3_in_triangulation_3.h>
#include <CGAL/Mesh_criteria_3.h>
#include <CGAL/Polyhedral_mesh_domain_3.h>
#include <CGAL/make_mesh_3.h>
#include <CGAL/refine_mesh_3.h>
#include <CGAL/Polyhedron_incremental_builder_3.h>

// AABB tree
#include <CGAL/Simple_cartesian.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits.h>
#include <CGAL/AABB_triangle_primitive.h>

// utils
#include <CGAL/Side_of_triangle_mesh.h>

class SegmentHash
{
public:
	size_t operator()(const Segment &s) const
	{
		std::size_t seed = 0;
		boost::hash_combine(seed, s.first.x);
		boost::hash_combine(seed, s.first.y);
		boost::hash_combine(seed, s.first.z);
		boost::hash_combine(seed, s.second.x);
		boost::hash_combine(seed, s.second.y);
		boost::hash_combine(seed, s.second.z);

		return seed;
	}
};

template <typename T>
float PointToSegmentDistanceSq(const T& p, const T& a, const T& b)
{
	auto v = b - a;
	auto w = p - a;

	auto c1 = glm::dot(w, v);
	if (c1 <= 0)
	{
		auto diffVec = p - a;
		return glm::dot(diffVec, diffVec);
	}

	auto c2 = glm::dot(v, v);
	if (c2 <= c1)
	{
		auto diffVec = p - b;
		return glm::dot(diffVec, diffVec);
	}
		
	auto t = c1 / c2;
	auto pb = a + t * v;

	auto diffVec = p - pb;
	return glm::dot(diffVec, diffVec);
}



class Voronoi
{
	// AABB
	//typedef CGAL::Simple_cartesian<double> AK;
	typedef CGAL::Exact_predicates_inexact_constructions_kernel AK;
	typedef AK::FT FT;
	typedef AK::Ray_3 ARay;
	typedef AK::Line_3 ALine;
	typedef AK::Point_3 APoint;
	typedef AK::Segment_3 ASegment;
	typedef AK::Triangle_3 ATriangle;
	typedef std::vector<ATriangle>::iterator Iterator;
	typedef CGAL::AABB_triangle_primitive<AK, Iterator> Primitive;
	typedef CGAL::AABB_traits<AK, Primitive> AABB_triangle_traits;
	typedef CGAL::AABB_tree<AABB_triangle_traits> Tree;
	typedef AABB_triangle_traits::Intersection_and_primitive_id<ARay>::Type IntersectionResult;

	// Meshing
	typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
	typedef CGAL::Polyhedron_3<K> Polyhedron;

	typedef CGAL::Polyhedral_mesh_domain_3<Polyhedron, K> BaseMeshDomain;
	class VoronoiMeshDomain : public BaseMeshDomain
	{
	public:
		VoronoiMeshDomain(const Polyhedron& p) : BaseMeshDomain(p) {}

		struct Construct_initial_points : public BaseMeshDomain::Construct_initial_points
		{
			Construct_initial_points(const Polyhedral_mesh_domain_3& domain) : BaseMeshDomain::Construct_initial_points(domain) {}

			template<class OutputIterator>
			OutputIterator operator()(OutputIterator pts) const
			{
				return BaseMeshDomain::Construct_initial_points::operator()(pts, 128);
			}
		};

		Construct_initial_points construct_initial_points_object() const
		{
			return Construct_initial_points(*this);
		}
	};
	typedef VoronoiMeshDomain Mesh_domain;
	//typedef CGAL::Polyhedral_mesh_domain_3<Polyhedron, K> Mesh_domain;
	typedef CGAL::Mesh_triangulation_3<Mesh_domain>::type Tr;
	typedef CGAL::Mesh_complex_3_in_triangulation_3<Tr> C3t3;
	typedef CGAL::Mesh_criteria_3<Tr> Mesh_criteria;

	typedef Polyhedron::HalfedgeDS HalfedgeDS;
	typedef HalfedgeDS::Vertex Vertex;
	typedef Vertex::Point Point;

	typedef std::vector<float> VertexBuffer;
	typedef std::vector<uint32_t> IndexBuffer;

	typedef std::unordered_set<Segment, SegmentHash> UniqueSegments;

public:
	Voronoi(const Settings& settings) : settings_(settings)
	{
		GenerateCells();
	}

	std::vector<Segment> GetEdges() const
	{
		std::vector<std::pair<glm::vec3, glm::vec3>> result;
		result.resize(uniqueSegments_.size());
		std::copy(uniqueSegments_.begin(), uniqueSegments_.end(), result.begin());
		return result;
	}

private:
	Polyhedron CreatePolyhedron(const VertexBuffer vb, const IndexBuffer& ib)
	{
		// Create input polyhedron
		Polyhedron polyhedron;

		typedef HalfedgeDS::Vertex Vertex;
		typedef Vertex::Point Point;
		CGAL::Polyhedron_incremental_builder_3<HalfedgeDS> builder(polyhedron.hds(), true);
		builder.begin_surface(vb.size() / 3, ib.size() / 3, 0);
		for (size_t i = 0; i < vb.size(); i += 3)
		{
			builder.add_vertex(Point(vb[i + 0], vb[i + 1], vb[i + 2]));
		}

		for (size_t i = 0; i < ib.size(); i += 3)
		{
			builder.begin_facet();
			builder.add_vertex_to_facet(ib[i + 0]);
			builder.add_vertex_to_facet(ib[i + 1]);
			builder.add_vertex_to_facet(ib[i + 2]);
			builder.end_facet();
		}
		builder.end_surface();

		ASSERT(!builder.error());
		return polyhedron;
	}

	void InitializeAABBTree(const VertexBuffer vb, const IndexBuffer& ib, Tree& tree, std::vector<ATriangle>& triangles)
	{
		triangles.reserve(ib.size() / 3);

		for (size_t i = 0; i < ib.size(); i += 3)
		{
			glm::vec3 a(vb[ib[i + 0] * 3 + 0], vb[ib[i + 0] * 3 + 1], vb[ib[i + 0] * 3 + 2]);
			glm::vec3 b(vb[ib[i + 1] * 3 + 0], vb[ib[i + 1] * 3 + 1], vb[ib[i + 1] * 3 + 2]);
			glm::vec3 c(vb[ib[i + 2] * 3 + 0], vb[ib[i + 2] * 3 + 1], vb[ib[i + 2] * 3 + 2]);

			triangles.push_back(ATriangle(APoint(a.x, a.y, a.z), APoint(b.x, b.y, b.z), APoint(c.x, c.y, c.z)));
		}

		tree.insert(triangles.begin(), triangles.end());
	}

	void GenerateCells()
	{
		using namespace CGAL::parameters;

		VertexBuffer vb;
		IndexBuffer ib;
		LoadStl(settings_.modelFile, vb, ib);

		auto polyhedron = CreatePolyhedron(vb, ib);
		Mesh_domain domain(polyhedron);

		Mesh_criteria criteria(facet_angle = 30, facet_distance = settings_.facetDistance, cell_size = settings_.cellSize, facet_size = settings_.facetSize);

		// Mesh generation
		C3t3 c3t3 = CGAL::make_mesh_3<C3t3>(domain, criteria, odt(), lloyd());

		std::vector<ATriangle> triangles;
		Tree tree;
		InitializeAABBTree(vb, ib, tree, triangles);
		CollectEdges(c3t3, polyhedron, tree);
	}

	glm::vec3 FindNearestMeshSurfaceIntersection(const Tree& tree, const ARay& ray)
	{
		std::vector<IntersectionResult> intersections;
		tree.all_intersections(ray, std::back_inserter(intersections));
		ASSERT(!intersections.empty());
		glm::vec3 rayOrigin(ray.start().x(), ray.start().y(), ray.start().z());
		auto it = std::min_element(intersections.begin(), intersections.end(), [&rayOrigin](const IntersectionResult& r1, const IntersectionResult& r2)
		{
			const auto p1 = boost::get<APoint>(r1.first);
			const auto p2 = boost::get<APoint>(r2.first);
			return glm::distance2(rayOrigin, glm::vec3(p1.x(), p1.y(), p1.z())) < glm::distance2(rayOrigin, glm::vec3(p2.x(), p2.y(), p2.z()));
		});

		const auto p = boost::get<APoint>(it->first);
		return glm::vec3(p.x(), p.y(), p.z());
	}

	void CollectSegment(const K::Segment_3& s, const Tree& tree, const CGAL::Side_of_triangle_mesh<Polyhedron, K>& insidePredicat)
	{
		glm::vec3 a(CGAL::to_double(s.source().x()), CGAL::to_double(s.source().y()), CGAL::to_double(s.source().z()));
		glm::vec3 b(CGAL::to_double(s.target().x()), CGAL::to_double(s.target().y()), CGAL::to_double(s.target().z()));

		const auto isAInside = insidePredicat(s.source()) == CGAL::ON_BOUNDED_SIDE;
		const auto isBInside = insidePredicat(s.target()) == CGAL::ON_BOUNDED_SIDE;
		if (isAInside && isBInside)
		{
			AddSegment(std::make_pair(a, b));
		}
		if (isAInside ^ isBInside)
		{
			if (!settings_.generateSticks)
			{
				return;
			}

			if (isBInside)
			{
				std::swap(a, b);
			}
			const auto p = FindNearestMeshSurfaceIntersection(tree, ARay(APoint(a.x, a.y, a.z), APoint(b.x, b.y, b.z)));
			AddSegment(std::make_pair(a, p));
		}
	}

	void CollectSegmentFromRay(const K::Ray_3& r, const Tree& tree, const CGAL::Side_of_triangle_mesh<Polyhedron, K>& insidePredicat)
	{
		if (!settings_.generateSticks)
		{
			return;
		}

		if (insidePredicat(r.source()) == CGAL::ON_BOUNDED_SIDE)
		{
			glm::vec3 a(CGAL::to_double(r.source().x()), CGAL::to_double(r.source().y()), CGAL::to_double(r.source().z()));
			glm::vec3 b(CGAL::to_double(r.direction().dx()), CGAL::to_double(r.direction().dy()), CGAL::to_double(r.direction().dz()));

			const auto p = FindNearestMeshSurfaceIntersection(tree, ARay(APoint(a.x, a.y, a.z), APoint(a.x + b.x, a.y + b.y, a.z + b.z)));
			AddSegment(std::make_pair(a, p));
		}
	}

	void CollectEdges(const C3t3& c3t3, const Polyhedron& polyhedron, const Tree& tree)
	{
		CGAL::Side_of_triangle_mesh<Polyhedron, K> isInside(polyhedron);
		const auto& tri = c3t3.triangulation();

		for (auto cellIt = tri.finite_cells_begin(); cellIt != tri.finite_cells_end(); ++cellIt)
		{
			for (int i = 0; i < 4; ++i)
			{
				const auto o = tri.dual(cellIt, i);
				if (const auto s = CGAL::object_cast<K::Segment_3>(&o))
				{
					CollectSegment(*s, tree, isInside);
				}
				else
				if (const auto r = CGAL::object_cast<K::Ray_3>(&o))
				{
					CollectSegmentFromRay(*r, tree, isInside);
				}
			}
		}
	}

	void AddSegment(const Segment& seg)
	{
		auto reversedSegment(seg);
		std::swap(reversedSegment.first, reversedSegment.second);
		if (uniqueSegments_.count(reversedSegment) == 0)
		{
			uniqueSegments_.insert(seg);
		}
	}

	const Settings settings_;
	UniqueSegments uniqueSegments_;
};

std::vector<Segment> GenerateVoronoiEdges(const Settings& settings)
{
	Voronoi voronoi(settings);
	return voronoi.GetEdges();
}


#if 0
// polygonize
#include <CGAL/Surface_mesh_default_triangulation_3.h>
#include <CGAL/Complex_2_in_triangulation_3.h>
#include <CGAL/make_surface_mesh.h>
#include <CGAL/Implicit_surface_3.h>
#include <CGAL/IO/output_surface_facets_to_polyhedron.h>
#endif

#if 0
struct SegmentData
{
	SegmentData() : segment(std::make_pair(glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::max()))) {}
	SegmentData(const Segment& s) : segment(s) {}
	Segment segment;
};

const auto BoxSize = 50.0f;
auto g_minX = -BoxSize / 2;
auto g_minY = -BoxSize / 2;
auto g_minZ = -BoxSize / 2;

auto g_maxX = BoxSize / 2;
auto g_maxY = BoxSize / 2;
auto g_maxZ = BoxSize / 2;

const auto CellCountX = 10;
const auto CellCountY = 10;
const auto CellCountZ = 10;
const auto MaxEdges = 50;
std::vector<SegmentData> segmentData(CellCountX*CellCountY*CellCountZ * MaxEdges);
#endif

#if 0
void VoronoiCGAL(const Settings& settings)
{
	std::vector<std::pair<float, size_t>> segPower;
	segPower.reserve(MaxEdges);
	auto index = 0;
	const auto StepX = (g_maxX - g_minX) / CellCountX;
	const auto StepY = (g_maxY - g_minY) / CellCountY;
	const auto StepZ = (g_maxZ - g_minZ) / CellCountZ;

	const auto IncludeRadiusSq = glm::length2(glm::vec3(StepX, StepY, StepZ));

	const glm::vec3 StartP(g_minX + StepX / 2, g_minY + StepY / 2, g_minZ + StepZ / 2);
	glm::vec3 p = StartP;
	for (int z = 0; z < CellCountZ; ++z, p.z += StepZ)
	{
		p.y = StartP.y;
		for (int y = 0; y < CellCountY; ++y, p.y += StepY)
		{
			p.x = StartP.x;
			for (int x = 0; x < CellCountX; ++x, ++index, p.x += StepX)
			{
				segPower.clear();
				for (size_t n = 0, size = segments.size(); n < size; ++n)
				{
					const auto dist = PointToSegmentDistanceSq(p, segments[n].first, segments[n].second);
					if (dist < IncludeRadiusSq)
					{
						segPower.push_back(std::make_pair(dist, n));
					}
				}
				std::sort(segPower.begin(), segPower.end(), [](const std::pair<float, size_t>& a, const std::pair<float, size_t>& b) { return a.first < b.first; });
				if (segPower.size() > MaxEdges)
				{
					segPower.resize(MaxEdges);
				}
				std::transform(segPower.begin(), segPower.end(), &segmentData[index*MaxEdges], [](const std::pair<float, size_t>& v) { return segments[v.second]; });
			}
		}
	}
}
#endif

#if 0
namespace pg
{

	/*Triangulation*/
	// default triangulation for Surface_mesher
	//typedef CGAL::Surface_mesh_default_triangulation_3 Tr;

	typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
	typedef CGAL::Surface_mesher::Surface_mesh_default_triangulation_3_generator<K>::Type Tr;
	// c2t3
	typedef CGAL::Complex_2_in_triangulation_3<Tr> C2t3;
	typedef Tr::Geom_traits GT;
	typedef GT::Sphere_3 Sphere_3;
	typedef GT::Point_3 Point_3;
	typedef GT::FT FT;
	typedef FT(*Function)(Point_3);
	typedef CGAL::Implicit_surface_3<GT, Function> Surface_3;

	typedef CGAL::Polyhedron_3<K> Polyhedron;

	struct PointGenerator
	{
		PointGenerator(const std::vector<Segment>& segments) : segments(segments)
		{

		}

		/*template<typename OutputIterator>
		OutputIterator operator()(OutputIterator pts)
		{
		size_t n = 0;
		std::transform(segments.begin(), segments.end(), pts, [&n](const Segment& s) {
		auto c = (s.first + s.second) * 0.5;
		Point_3 p(c.x, c.y, c.z);
		return std::make_pair(p, n++);
		});
		}*/

		template<typename OutputIterator>
		OutputIterator operator()(const Surface_3& s, OutputIterator pts, int n) const
		{
			ASSERT(n <= segments.size());
			std::transform(segments.begin(), segments.begin() + n, pts, [](const Segment& s) {
				auto c = (s.first + s.second) * 0.5f;
				return Point_3(c.x, c.y, c.z);
			});

			return pts;
		}

		const std::vector<Segment>& segments;
	};

	struct SurfTraits : public CGAL::Surface_mesh_traits_generator_3<Surface_3>::type
	{
		SurfTraits(const std::vector<Segment>& segments) : segments(segments) {}

		typedef PointGenerator Construct_initial_points;
		PointGenerator construct_initial_points_object() const
		{
			return PointGenerator(segments);
		}
		const std::vector<Segment>& segments;
	};

	void Polygonize(const std::string& fileName)
	{
		auto surface_function = [](Point_3 p) -> FT
		{
			glm::vec3 point(p.x(), p.y(), p.z());
			if (point.x < g_minX || point.x >= g_maxX) return 1000;
			if (point.y < g_minY || point.y >= g_maxY) return 1000;
			if (point.z < g_minZ || point.z >= g_maxZ) return 1000;
			int xCell = (CellCountX * (point.x - g_minX) / (g_maxX - g_minX));
			int yCell = (CellCountY * (point.y - g_minY) / (g_maxY - g_minY));
			int zCell = (CellCountZ * (point.z - g_minZ) / (g_maxZ - g_minZ));

			float field = std::numeric_limits<float>::max();
			int index = zCell * (CellCountX*CellCountY) + yCell*CellCountX + xCell;
			for (int i = 0; i < MaxEdges && segmentData[index * MaxEdges + i].segment.first.x < std::numeric_limits<float>::max(); ++i)
			{
				const auto dSq = PointToSegmentDistanceSq(point, segmentData[index * MaxEdges + i].segment.first, segmentData[index * MaxEdges + i].segment.second);
				field = std::min(field, dSq);
			}

			const auto StickWidth = 0.18;
			const auto SurfaceThreshold = (StickWidth / 2)*(StickWidth / 2);
			return field - SurfaceThreshold;

			/*
			glm::vec3 point(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
			float field = std::numeric_limits<float>::max();
			for (const auto& seg : segments)
			{
			float dSq = PointToSegmentDistanceSq(point, seg.first, seg.second);
			field = std::min(field, dSq);
			}
			const auto SurfaceThreshold = 0.18*0.18;
			return field - SurfaceThreshold;
			*/
		};

		Tr tr;            // 3D-Delaunay triangulation
		C2t3 c2t3(tr);   // 2D-complex in 3D-Delaunay triangulation
						 // defining the surface
		const auto Radius = 100.0;
		Surface_3 surface(surface_function, Sphere_3(CGAL::ORIGIN, Radius * Radius));

		const auto AngularBound = 15.0;
		const auto RadiusBound = 0.075;
		const auto DistanceBound = 0.05;
		CGAL::Surface_mesh_default_criteria_3<Tr> criteria(AngularBound, RadiusBound, DistanceBound);
		CGAL::make_surface_mesh(c2t3, surface, SurfTraits(segments), criteria, CGAL::Manifold_with_boundary_tag(), segments.size());

		Polyhedron poly;
		CGAL::output_surface_facets_to_polyhedron(c2t3, poly);


		std::fstream file(fileName, std::ios::output);

		file << "solid\n";
		for (auto it = poly.facets_begin(); it != poly.facets_end(); ++it)
		{
			file << "facet normal 0 0 0\n";
			file << "outer loop\n";

			auto fit = it->facet_begin();
			do
			{
				auto pt = fit->vertex()->point();
				file << "vertex " << pt.x() << " " << pt.y() << " " << pt.z() << "\n";
			} while (++fit != it->facet_begin());

			file << "endloop\n";
			file << "endfacet\n";
		}
		file << "endsolid";

		file.close();
	}

} // namespace pg
#endif
