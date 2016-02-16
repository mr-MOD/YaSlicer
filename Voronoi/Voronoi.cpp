#include "Voronoi.h"
#include "Loaders.h"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <glm/gtx/normal.hpp>

#include <unordered_set>
#include <algorithm>
#include <memory>
#include <string>

#include <boost/functional/hash.hpp>


// polygonize
#include <CGAL/Surface_mesh_default_triangulation_3.h>
#include <CGAL/Complex_2_in_triangulation_3.h>
#include <CGAL/make_surface_mesh.h>
#include <CGAL/Implicit_surface_3.h>
#include <CGAL/IO/output_surface_facets_to_polyhedron.h>

// voronoi
#define CGAL_CONCURRENT_MESH_3
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
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

using Segment = std::pair<glm::vec3, glm::vec3>;



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

// This function returns a random double between 0 and 1
double rnd() { return double(rand()) / RAND_MAX; }

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


std::unordered_set<Segment, SegmentHash> segUnique;
std::vector<Segment> segments;

void addSegment(const Segment& seg)
{
	auto reversedSegment(seg);
	std::swap(reversedSegment.first, reversedSegment.second);
	if (segUnique.count(reversedSegment) == 0)
	{
		segUnique.insert(seg);
	}
}

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
		"finite\n"
		"70\n"
		"64\n"
		"62\n"
		"7\n"
		"6\n"
		"CONTINUOUS\n"
		"0\n"
		"LAYER\n"
		"2\n"
		"infinite\n"
		"70\n"
		"64\n"
		"62\n"
		"5\n"
		"6\n"
		"CONTINUOUS\n"
		"0\n"
		"LAYER\n"
		"2\n"
		"shell\n"
		"70\n"
		"64\n"
		"62\n"
		"3\n"
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

void WriteDXFLine(std::ostream& out, const std::pair<glm::vec3, glm::vec3>& line, int layer)
{
	const char * layers[] =
	{
		"finite",
		"infinite",
		"shell"
	};

	out <<
		"0\n"
		"LINE\n"
		"8\n" <<
		layers[layer] << "\n" <<
		"10\n" <<
		line.first.x /*+ (rnd() - 0.5) / 1000.0*/ << "\n"
		"20\n" <<
		line.first.y /*+ (rnd() - 0.5) / 1000.0*/ << "\n"
		"30\n" <<
		line.first.z /*+ (rnd() - 0.5) / 1000.0*/ << "\n"
		"11\n" <<
		line.second.x /*+ (rnd() - 0.5) / 1000.0*/ << "\n"
		"21\n" <<
		line.second.y /*+ (rnd() - 0.5) / 1000.0*/ << "\n"
		"31\n" <<
		line.second.z /*+ (rnd() - 0.5) / 1000.0*/ << "\n";
}

void EndDXFSection(std::ostream& out)
{
	out <<
		"0\n"
		"ENDSEC\n"
		"0\n"
		"EOF\n";
}

void VoronoiCGAL(const std::string& stlFile)
{
	// AABB
	typedef CGAL::Simple_cartesian<double> AK;
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

	// Meshing
	// Domain
	typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
	typedef CGAL::Polyhedron_3<K> Polyhedron;
	typedef CGAL::Polyhedral_mesh_domain_3<Polyhedron, K> Mesh_domain;
	// Triangulation
#ifdef CGAL_CONCURRENT_MESH_3
	typedef CGAL::Mesh_triangulation_3<
		Mesh_domain,
		CGAL::Kernel_traits<Mesh_domain>::Kernel, // Same as sequential
		CGAL::Sequential_tag                        // Tag to activate parallelism
	>::type Tr;
#else
	typedef CGAL::Mesh_triangulation_3<Mesh_domain>::type Tr;
#endif
	typedef CGAL::Mesh_complex_3_in_triangulation_3<Tr> C3t3;
	// Criteria
	typedef CGAL::Mesh_criteria_3<Tr> Mesh_criteria;

	typedef Polyhedron::HalfedgeDS HalfedgeDS;
	// To avoid verbose function and named parameters call
	using namespace CGAL::parameters;
	
	std::vector<float> vb;
	std::vector<uint32_t> ib;
	LoadStl(stlFile, vb, ib);

	// Create input polyhedron
	Polyhedron polyhedron;

	typedef HalfedgeDS::Vertex Vertex;
	typedef Vertex::Point Point;
	CGAL::Polyhedron_incremental_builder_3<HalfedgeDS> B(polyhedron.hds(), true);
	B.begin_surface(vb.size()/3, ib.size()/3, 0);
	for (size_t i = 0; i < vb.size(); i += 3)
	{
		B.add_vertex(Point(vb[i + 0], vb[i + 1], vb[i + 2]));
	}

	std::vector<ATriangle> triangles;
	triangles.reserve(ib.size()/3);

	for (size_t i = 0; i < ib.size(); i += 3)
	{
		B.begin_facet();
		B.add_vertex_to_facet(ib[i + 0]);
		B.add_vertex_to_facet(ib[i + 1]);
		B.add_vertex_to_facet(ib[i + 2]);
		B.end_facet();

		glm::vec3 a(vb[ib[i + 0] * 3 + 0], vb[ib[i + 0] * 3 + 1], vb[ib[i + 0] * 3 + 2]);
		glm::vec3 b(vb[ib[i + 1] * 3 + 0], vb[ib[i + 1] * 3 + 1], vb[ib[i + 1] * 3 + 2]);
		glm::vec3 c(vb[ib[i + 2] * 3 + 0], vb[ib[i + 2] * 3 + 1], vb[ib[i + 2] * 3 + 2]);

		triangles.push_back(ATriangle(APoint(a.x, a.y, a.z), APoint(b.x, b.y, b.z), APoint(c.x, c.y, c.z)));

		/*WriteDXFLine(dxf, std::make_pair(a, b), 2);
		WriteDXFLine(dxf, std::make_pair(b, c), 2);
		WriteDXFLine(dxf, std::make_pair(c, a), 2);*/
	}
	B.end_surface();

	Tree tree(triangles.begin(), triangles.end());

	// Create domain
	Mesh_domain domain(polyhedron);

	// Mesh criteria (no cell_size set)
	Mesh_criteria criteria(facet_angle = 30, facet_distance = 0.5, cell_size = 3);

	// Mesh generation
	C3t3 c3t3 = CGAL::make_mesh_3<C3t3>(domain, criteria, no_perturb(), no_exude());
	
	// Set tetrahedron size (keep cell_radius_edge_ratio), ignore facets
	//Mesh_criteria new_criteria(cell_size = 2);
	// Mesh refinement
//	CGAL::refine_mesh_3(c3t3, domain, new_criteria);

	CGAL::Side_of_triangle_mesh<Polyhedron, K> inside(polyhedron);

	typedef AABB_triangle_traits::Intersection_and_primitive_id<ARay>::Type IntersectionResult;
	std::vector<IntersectionResult> intersections;
	const auto& tri = c3t3.triangulation();

	std::vector<Tr::Cell_handle> cells;
	std::vector<Tr::Facet> facets;
	for (auto vtxIt = tri.finite_vertices_begin(); vtxIt != tri.finite_vertices_end(); ++vtxIt)
	{
		facets.clear();
		tri.finite_incident_facets(vtxIt, std::back_inserter(facets));
			
		for (const auto& facet : facets)
		{
			auto cell = facet.first;
			auto facetNumber = facet.second;
			if (cell->is_facet_on_surface(facetNumber))
			{
				const auto o = tri.dual(cell, facetNumber);
			}
		}
	}

	for (auto cellIt = tri.finite_cells_begin(); cellIt != tri.finite_cells_end(); ++cellIt)
	{
		for (int i = 0; i < 4; ++i)
		{
			const auto o = tri.dual(cellIt, i);
			if (const auto s = CGAL::object_cast<K::Segment_3>(&o))
			{
				glm::vec3 a(CGAL::to_double(s->source().x()), CGAL::to_double(s->source().y()), CGAL::to_double(s->source().z()));
				glm::vec3 b(CGAL::to_double(s->target().x()), CGAL::to_double(s->target().y()), CGAL::to_double(s->target().z()));

				const auto isAInside = inside(s->source()) == CGAL::ON_BOUNDED_SIDE;
				const auto isBInside = inside(s->target()) == CGAL::ON_BOUNDED_SIDE;
				if (isAInside && isBInside)
				{
					addSegment(std::make_pair(a, b));
					//WriteDXFLine(dxf, std::make_pair(a, b), 0);
				}
				else if (isAInside ^ isBInside)
				{
					if (isBInside)
					{
						std::swap(a, b);
					}
					intersections.clear();
					tree.all_intersections(ARay(APoint(a.x, a.y, a.z), APoint(b.x, b.y, b.z)), std::back_inserter(intersections));
					assert(!intersections.empty());
					auto it = std::min_element(intersections.begin(), intersections.end(), [&a](const IntersectionResult& r1, const IntersectionResult& r2)
					{
						const auto p1 = boost::get<APoint>(r1.first);
						const auto p2 = boost::get<APoint>(r2.first);
						return glm::distance2(a, glm::vec3(p1.x(), p1.y(), p1.z())) < glm::distance2(a, glm::vec3(p2.x(), p2.y(), p2.z()));
					});

					const auto p = boost::get<APoint>(it->first);
					addSegment(std::make_pair(a, glm::vec3(p.x(), p.y(), p.z())));
					//WriteDXFLine(dxf, std::make_pair(a, glm::vec3(p.x(), p.y(), p.z())), 0);
				}
			}
			else
			if (const auto r = CGAL::object_cast<K::Ray_3>(&o))
			{
				if (inside(r->source()) == CGAL::ON_BOUNDED_SIDE)
				{
					glm::vec3 a(CGAL::to_double(r->source().x()), CGAL::to_double(r->source().y()), CGAL::to_double(r->source().z()));
					glm::vec3 b(CGAL::to_double(r->direction().dx()), CGAL::to_double(r->direction().dy()), CGAL::to_double(r->direction().dz()));

					intersections.clear();
					tree.all_intersections(ARay(APoint(a.x, a.y, a.z), APoint(a.x + b.x, a.y + b.y, a.z + b.z)), std::back_inserter(intersections));
					assert(!intersections.empty());
					auto it = std::min_element(intersections.begin(), intersections.end(), [&a](const IntersectionResult& r1, const IntersectionResult& r2)
					{
						const auto p1 = boost::get<APoint>(r1.first);
						const auto p2 = boost::get<APoint>(r2.first);
						return glm::distance2(a, glm::vec3(p1.x(), p1.y(), p1.z())) < glm::distance2(a, glm::vec3(p2.x(), p2.y(), p2.z()));
					});

					const auto p = boost::get<APoint>(it->first);
					addSegment(std::make_pair(a, glm::vec3(p.x(), p.y(), p.z())));
					//WriteDXFLine(dxf, std::make_pair(a, glm::vec3(p.x(), p.y(), p.z())), 1);
				}
			}
		}
	}

	


#if 0
	for (size_t i = 0; i < ib.size(); i += 3)
	{
		const auto i0 = ib[i + 0];
		const auto i1 = ib[i + 1];
		const auto i2 = ib[i + 2];
		glm::vec3 a(vb[i0 * 3 + 0], vb[i0 * 3 + 1], vb[i0 * 3 + 2]);
		glm::vec3 b(vb[i1 * 3 + 0], vb[i1 * 3 + 1], vb[i1 * 3 + 2]);
		glm::vec3 c(vb[i2 * 3 + 0], vb[i2 * 3 + 1], vb[i2 * 3 + 2]);
		addSegment(std::make_pair(a, b));
		addSegment(std::make_pair(b, c));
		addSegment(std::make_pair(c, a));
	}
#endif

	segments.reserve(segUnique.size());
	std::copy(segUnique.begin(), segUnique.end(), std::back_inserter(segments));

	std::fstream dxf("out.dxf", std::ios::out);
	WriteDXFHeaders(dxf);
	BeginDXFSection(dxf);

	g_maxX = g_maxY = g_maxZ = std::numeric_limits<float>::lowest();
	g_minX = g_minY = g_minZ = std::numeric_limits<float>::max();
	for (const auto& seg : segUnique)
	{
		WriteDXFLine(dxf, seg, 0);
		g_maxX = std::max(g_maxX, seg.first.x);
		g_maxX = std::max(g_maxX, seg.second.x);
		g_maxY = std::max(g_maxY, seg.first.y);
		g_maxY = std::max(g_maxY, seg.second.y);
		g_maxZ = std::max(g_maxZ, seg.first.z);
		g_maxZ = std::max(g_maxZ, seg.second.z);

		g_minX = std::min(g_minX, seg.first.x);
		g_minX = std::min(g_minX, seg.second.x);
		g_minY = std::min(g_minY, seg.first.y);
		g_minY = std::min(g_minY, seg.second.y);
		g_minZ = std::min(g_minZ, seg.first.z);
		g_minZ = std::min(g_minZ, seg.second.z);
	}
	EndDXFSection(dxf);

	

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
		assert(n <= segments.size());
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

void Polygonize()
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
		const auto SurfaceThreshold = (StickWidth/2)*(StickWidth/2);
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

	std::cout << "solid\n";

	for (auto it = poly.facets_begin(); it != poly.facets_end(); ++it)
	{
		std::cout << "facet normal 0 0 0\n";
		std::cout << "outer loop\n";

		auto fit = it->facet_begin();
		do
		{
			auto pt = fit->vertex()->point();
			std::cout << "vertex " << pt.x() << " " << pt.y() << " " << pt.z() << "\n";
		} while (++fit != it->facet_begin());

		std::cout << "endloop\n";
		std::cout << "endfacet\n";
	}
	std::cout << "endsolid";
}

} // namespace pg
#endif

void Test()
{
	VoronoiCGAL("form.stl");
	//pg::Polygonize();
}
