#include "gaussian_field.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <numeric>
#include <sstream>

#include "util/util.h"

namespace {

// Real spherical-harmonics constants of the 3DGS reference implementation.
constexpr double kC0 = 0.28209479177387814;
constexpr double kC1 = 0.4886025119029199;
constexpr double kC2[5] = {1.0925484305920792, -1.0925484305920792,
                           0.31539156525252005, -1.0925484305920792,
                           0.5462742152960396};
constexpr double kC3[7] = {-0.5900435899266435, 2.890611442640554,
                           -0.4570457994644658, 0.3731763325901154,
                           -0.4570457994644658, 1.445305721320277,
                           -0.5900435899266435};

// Rotation matrix of a unit quaternion (w, x, y, z), row-major.
void quat_to_matrix(const double q[4], double R[9]) {
  const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  const double w = q[0] / n, x = q[1] / n, y = q[2] / n, z = q[3] / n;
  R[0] = 1 - 2 * (y * y + z * z); R[1] = 2 * (x * y - w * z);     R[2] = 2 * (x * z + w * y);
  R[3] = 2 * (x * y + w * z);     R[4] = 1 - 2 * (x * x + z * z); R[5] = 2 * (y * z - w * x);
  R[6] = 2 * (x * z - w * y);     R[7] = 2 * (y * z + w * x);     R[8] = 1 - 2 * (x * x + y * y);
}

void mat_mul(const double A[9], const double B[9], double C[9]) {
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      C[i * 3 + j] = A[i * 3] * B[j] + A[i * 3 + 1] * B[3 + j] + A[i * 3 + 2] * B[6 + j];
}

struct ply_property {
  std::string name;
  int size = 0;
  int offset = 0;
  char kind = 'f'; // f: float, d: double, i: signed, u: unsigned
};

bool property_type(const std::string &t, int &size, char &kind) {
  if (t == "float" || t == "float32") { size = 4; kind = 'f'; }
  else if (t == "double" || t == "float64") { size = 8; kind = 'd'; }
  else if (t == "uchar" || t == "uint8") { size = 1; kind = 'u'; }
  else if (t == "char" || t == "int8") { size = 1; kind = 'i'; }
  else if (t == "ushort" || t == "uint16") { size = 2; kind = 'u'; }
  else if (t == "short" || t == "int16") { size = 2; kind = 'i'; }
  else if (t == "uint" || t == "uint32") { size = 4; kind = 'u'; }
  else if (t == "int" || t == "int32") { size = 4; kind = 'i'; }
  else return false;
  return true;
}

// Little-endian value of a property (the reader requires a little-endian
// file and checks the host).
double read_value(const unsigned char *p, const ply_property &prop) {
  switch (prop.kind) {
  case 'f': { float v; std::memcpy(&v, p, 4); return v; }
  case 'd': { double v; std::memcpy(&v, p, 8); return v; }
  case 'u': {
    uint32_t v = 0;
    std::memcpy(&v, p, static_cast<size_t>(prop.size));
    return static_cast<double>(v);
  }
  default:
    if (prop.size == 1) { int8_t v; std::memcpy(&v, p, 1); return v; }
    if (prop.size == 2) { int16_t v; std::memcpy(&v, p, 2); return v; }
    { int32_t v; std::memcpy(&v, p, 4); return v; }
  }
}

bool host_is_little_endian() {
  const uint16_t one = 1;
  unsigned char b;
  std::memcpy(&b, &one, 1);
  return b == 1;
}

thread_local std::vector<int> t_stack;

} // namespace

color gaussian_field::eval_sh(int degree, const float *sh, const vec3 &dir_in) {
  const vec3 d = unit_vector(dir_in);
  const double x = d.x(), y = d.y(), z = d.z();
  double v[3];
  for (int c = 0; c < 3; ++c) {
    const auto s = [&](int k) { return static_cast<double>(sh[k * 3 + c]); };
    double r = kC0 * s(0);
    if (degree > 0) {
      r += -kC1 * y * s(1) + kC1 * z * s(2) - kC1 * x * s(3);
      if (degree > 1) {
        const double xx = x * x, yy = y * y, zz = z * z;
        const double xy = x * y, yz = y * z, xz = x * z;
        r += kC2[0] * xy * s(4) + kC2[1] * yz * s(5) +
             kC2[2] * (2 * zz - xx - yy) * s(6) + kC2[3] * xz * s(7) +
             kC2[4] * (xx - yy) * s(8);
        if (degree > 2) {
          r += kC3[0] * y * (3 * xx - yy) * s(9) + kC3[1] * xy * z * s(10) +
               kC3[2] * y * (4 * zz - xx - yy) * s(11) +
               kC3[3] * z * (2 * zz - 3 * xx - 3 * yy) * s(12) +
               kC3[4] * x * (4 * zz - xx - yy) * s(13) +
               kC3[5] * z * (xx - yy) * s(14) + kC3[6] * x * (xx - 3 * yy) * s(15);
        }
      }
    }
    v[c] = std::max(0.0, r + 0.5);
  }
  return color(v[0], v[1], v[2]);
}

bool gaussian_field::add(const vec3 &mean, const vec3 &scale, const double quat[4],
                         double opacity, const float *sh, int degree) {
  opacity = std::min(opacity, kMaxOpacity);
  if (!(opacity >= kMinAlpha) || degree < 0 || degree > 3)
    return false;
  double R[9];
  quat_to_matrix(quat, R);
  const double s[3] = {scale.x(), scale.y(), scale.z()};
  if (!(s[0] > 0 && s[1] > 0 && s[2] > 0))
    return false;

  gaussian g{};
  for (int i = 0; i < 3; ++i) {
    g.mean[i] = static_cast<float>(mean[i]);
    for (int j = 0; j < 3; ++j)
      g.whiten[i * 3 + j] = static_cast<float>(R[j * 3 + i] / s[i]); // S^-1 R^T
  }
  g.opacity = static_cast<float>(opacity);
  const int n = (degree + 1) * (degree + 1) * 3;
  std::copy(sh, sh + n, g.sh);
  degree_ = std::max(degree_, degree);

  // Support: alpha = o exp(-d^2 / 2) >= kMinAlpha  <=>  d^2 <= 2 ln(o / kMinAlpha).
  const double k = std::sqrt(2.0 * std::log(opacity / kMinAlpha));
  for (int j = 0; j < 3; ++j) {
    double var = 0.0; // Sigma_jj = sum_k R_jk^2 s_k^2
    for (int c = 0; c < 3; ++c)
      var += R[j * 3 + c] * R[j * 3 + c] * s[c] * s[c];
    const double h = k * std::sqrt(var);
    box_lo_.push_back(static_cast<float>(mean[j] - h));
    box_hi_.push_back(static_cast<float>(mean[j] + h));
  }
  g_.push_back(g);
  return true;
}

bool gaussian_field::load_ply(const std::string &path, std::string *error) {
  auto fail = [&](const std::string &msg) {
    if (error)
      *error = path + ": " + msg;
    return false;
  };
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return fail("cannot open");
  std::string line;
  std::getline(f, line);
  if (line != "ply")
    return fail("not a PLY file");
  long long count = -1;
  bool in_vertex = false, little = false;
  std::vector<ply_property> props;
  int stride = 0;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    std::istringstream ls(line);
    std::string word;
    ls >> word;
    if (word == "format") {
      std::string fmt;
      ls >> fmt;
      little = fmt == "binary_little_endian";
    } else if (word == "element") {
      std::string name;
      long long n;
      ls >> name >> n;
      in_vertex = name == "vertex";
      if (in_vertex)
        count = n;
      else if (count >= 0 && n > 0)
        return fail("elements after 'vertex' are not supported");
    } else if (word == "property" && in_vertex) {
      std::string type, name;
      ls >> type >> name;
      if (type == "list")
        return fail("list properties are not supported");
      ply_property p;
      if (!property_type(type, p.size, p.kind))
        return fail("unknown property type '" + type + "'");
      p.name = name;
      p.offset = stride;
      stride += p.size;
      props.push_back(p);
    } else if (word == "end_header") {
      break;
    }
  }
  if (!little)
    return fail("only binary_little_endian PLY is supported");
  if (!host_is_little_endian())
    return fail("big-endian hosts are not supported");
  if (count < 0)
    return fail("no vertex element");

  auto find = [&](const std::string &name) -> const ply_property * {
    for (const auto &p : props)
      if (p.name == name)
        return &p;
    return nullptr;
  };
  const char *required[] = {"x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity",
                            "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"};
  for (const char *name : required)
    if (!find(name))
      return fail(std::string("missing property '") + name + "'");
  int n_rest = 0;
  while (find("f_rest_" + std::to_string(n_rest)))
    ++n_rest;
  int degree = 0;
  while ((degree + 1) * (degree + 1) - 1 < n_rest / 3)
    ++degree;
  if (n_rest % 3 != 0 || (degree + 1) * (degree + 1) - 1 != n_rest / 3 || degree > 3)
    return fail("unsupported number of SH coefficients (" + std::to_string(n_rest) + " f_rest)");
  const int per_channel = n_rest / 3;

  std::vector<const ply_property *> rest(static_cast<size_t>(n_rest));
  for (int k = 0; k < n_rest; ++k)
    rest[static_cast<size_t>(k)] = find("f_rest_" + std::to_string(k));
  const ply_property *px = find("x"), *py = find("y"), *pz = find("z");
  const ply_property *dc[3] = {find("f_dc_0"), find("f_dc_1"), find("f_dc_2")};
  const ply_property *op = find("opacity");
  const ply_property *sc[3] = {find("scale_0"), find("scale_1"), find("scale_2")};
  const ply_property *rot[4] = {find("rot_0"), find("rot_1"), find("rot_2"), find("rot_3")};

  std::vector<unsigned char> rec(static_cast<size_t>(stride));
  std::vector<float> sh(48);
  g_.reserve(g_.size() + static_cast<size_t>(count));
  for (long long i = 0; i < count; ++i) {
    if (!f.read(reinterpret_cast<char *>(rec.data()), stride))
      return fail("truncated vertex data");
    const unsigned char *p = rec.data();
    const vec3 mean(read_value(p + px->offset, *px), read_value(p + py->offset, *py),
                    read_value(p + pz->offset, *pz));
    const vec3 scale(std::exp(read_value(p + sc[0]->offset, *sc[0])),
                     std::exp(read_value(p + sc[1]->offset, *sc[1])),
                     std::exp(read_value(p + sc[2]->offset, *sc[2])));
    double q[4];
    for (int k = 0; k < 4; ++k)
      q[k] = read_value(p + rot[k]->offset, *rot[k]);
    const double opacity = 1.0 / (1.0 + std::exp(-read_value(p + op->offset, *op)));
    for (int c = 0; c < 3; ++c) {
      sh[static_cast<size_t>(c)] = static_cast<float>(read_value(p + dc[c]->offset, *dc[c]));
      // f_rest is channel-major: coefficient k of channel c is f_rest_{c * per_channel + k}.
      for (int k = 0; k < per_channel; ++k) {
        const ply_property *pr = rest[static_cast<size_t>(c * per_channel + k)];
        sh[static_cast<size_t>((k + 1) * 3 + c)] = static_cast<float>(read_value(p + pr->offset, *pr));
      }
    }
    add(mean, scale, q, opacity, sh.data(), degree); // drops invisible Gaussians
  }
  degree_ = std::max(degree_, degree);
  return true;
}

void gaussian_field::set_transform(const vec3 &position, double scale,
                                   const vec3 &rotation_degrees) {
  const double ax = degrees_to_radians(rotation_degrees.x());
  const double ay = degrees_to_radians(rotation_degrees.y());
  const double az = degrees_to_radians(rotation_degrees.z());
  const double Rx[9] = {1, 0, 0, 0, std::cos(ax), -std::sin(ax), 0, std::sin(ax), std::cos(ax)};
  const double Ry[9] = {std::cos(ay), 0, std::sin(ay), 0, 1, 0, -std::sin(ay), 0, std::cos(ay)};
  const double Rz[9] = {std::cos(az), -std::sin(az), 0, std::sin(az), std::cos(az), 0, 0, 0, 1};
  double Rxy[9], R[9];
  mat_mul(Rx, Ry, Rxy);
  mat_mul(Rxy, Rz, R);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      rot_[i * 3 + j] = R[j * 3 + i]; // R^T
  position_ = position;
  scale_ = scale;
}

void gaussian_field::to_local(const ray &r, point3 &o, vec3 &d) const {
  const vec3 a = (r.origin() - position_) / scale_;
  const vec3 b = r.direction() / scale_;
  o = point3(rot_[0] * a[0] + rot_[1] * a[1] + rot_[2] * a[2],
             rot_[3] * a[0] + rot_[4] * a[1] + rot_[5] * a[2],
             rot_[6] * a[0] + rot_[7] * a[1] + rot_[8] * a[2]);
  d = vec3(rot_[0] * b[0] + rot_[1] * b[1] + rot_[2] * b[2],
           rot_[3] * b[0] + rot_[4] * b[1] + rot_[5] * b[2],
           rot_[6] * b[0] + rot_[7] * b[1] + rot_[8] * b[2]);
}

void gaussian_field::build() {
  nodes_.clear();
  order_.resize(g_.size());
  std::iota(order_.begin(), order_.end(), 0);
  if (!g_.empty())
    build_node(0, static_cast<int>(g_.size()), 0);
}

int gaussian_field::build_node(int first, int count, int depth) {
  const int index = static_cast<int>(nodes_.size());
  nodes_.push_back(node{});
  float lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
  float clo[3] = {INFINITY, INFINITY, INFINITY}, chi[3] = {-INFINITY, -INFINITY, -INFINITY};
  for (int i = first; i < first + count; ++i) {
    const int g = order_[static_cast<size_t>(i)];
    for (int a = 0; a < 3; ++a) {
      lo[a] = std::min(lo[a], box_lo_[static_cast<size_t>(g) * 3 + a]);
      hi[a] = std::max(hi[a], box_hi_[static_cast<size_t>(g) * 3 + a]);
      clo[a] = std::min(clo[a], g_[static_cast<size_t>(g)].mean[a]);
      chi[a] = std::max(chi[a], g_[static_cast<size_t>(g)].mean[a]);
    }
  }
  node n{};
  std::copy(lo, lo + 3, n.lo);
  std::copy(hi, hi + 3, n.hi);
  const int axis = (chi[0] - clo[0] >= chi[1] - clo[1] && chi[0] - clo[0] >= chi[2] - clo[2])
                       ? 0 : (chi[1] - clo[1] >= chi[2] - clo[2] ? 1 : 2);
  if (count <= 4 || depth >= 60 || chi[axis] <= clo[axis]) {
    n.first = first;
    n.count = count;
    nodes_[static_cast<size_t>(index)] = n;
    return index;
  }
  // Median split on the centroids.
  const int mid = first + count / 2;
  std::nth_element(order_.begin() + first, order_.begin() + mid, order_.begin() + first + count,
                   [&](int a, int b) {
                     return g_[static_cast<size_t>(a)].mean[axis] < g_[static_cast<size_t>(b)].mean[axis];
                   });
  const int left = build_node(first, mid - first, depth + 1);
  const int right = build_node(mid, first + count - mid, depth + 1);
  (void)left; // the left child always follows its parent
  n.first = right;
  n.count = 0;
  nodes_[static_cast<size_t>(index)] = n;
  return index;
}

void gaussian_field::collect(const point3 &o, const vec3 &d, double t_min, double t_max,
                             std::vector<hit> &out) const {
  out.clear();
  if (nodes_.empty())
    return;
  const double inv[3] = {1.0 / d.x(), 1.0 / d.y(), 1.0 / d.z()};
  std::vector<int> &stack = t_stack;
  stack.clear();
  stack.push_back(0);
  while (!stack.empty()) {
    const node &n = nodes_[static_cast<size_t>(stack.back())];
    const int self = stack.back();
    stack.pop_back();
    double t0 = t_min, t1 = t_max;
    bool miss = false;
    for (int a = 0; a < 3 && !miss; ++a) {
      double ta = (n.lo[a] - o[a]) * inv[a];
      double tb = (n.hi[a] - o[a]) * inv[a];
      if (ta > tb)
        std::swap(ta, tb);
      if (std::isnan(ta) || std::isnan(tb)) // origin on a slab with d[a] == 0
        continue;
      t0 = std::max(t0, ta);
      t1 = std::min(t1, tb);
      miss = t0 > t1;
    }
    if (miss)
      continue;
    if (n.count == 0) {
      stack.push_back(n.first);  // right child
      stack.push_back(self + 1); // left child
      continue;
    }
    for (int i = n.first; i < n.first + n.count; ++i) {
      const int gi = order_[static_cast<size_t>(i)];
      const gaussian &g = g_[static_cast<size_t>(gi)];
      const double rel[3] = {o.x() - g.mean[0], o.y() - g.mean[1], o.z() - g.mean[2]};
      double ol[3], dl[3];
      for (int r = 0; r < 3; ++r) {
        ol[r] = g.whiten[r * 3] * rel[0] + g.whiten[r * 3 + 1] * rel[1] + g.whiten[r * 3 + 2] * rel[2];
        dl[r] = g.whiten[r * 3] * d.x() + g.whiten[r * 3 + 1] * d.y() + g.whiten[r * 3 + 2] * d.z();
      }
      const double dd = dl[0] * dl[0] + dl[1] * dl[1] + dl[2] * dl[2];
      if (!(dd > 0.0))
        continue;
      const double t = -(ol[0] * dl[0] + ol[1] * dl[1] + ol[2] * dl[2]) / dd;
      if (!(t > t_min && t < t_max))
        continue;
      const double p0 = ol[0] + t * dl[0], p1 = ol[1] + t * dl[1], p2 = ol[2] + t * dl[2];
      const double alpha = g.opacity * std::exp(-0.5 * (p0 * p0 + p1 * p1 + p2 * p2));
      if (alpha >= kMinAlpha)
        out.push_back(hit{t, alpha, gi});
    }
  }
}

void gaussian_field::trace(const ray &r, double t_min, double t_max, color &radiance,
                           double &transmittance) const {
  static thread_local std::vector<hit> hits;
  point3 o;
  vec3 d;
  to_local(r, o, d);
  collect(o, d, t_min, t_max, hits);
  std::sort(hits.begin(), hits.end(), [](const hit &a, const hit &b) {
    return a.t < b.t || (a.t == b.t && a.index < b.index);
  });
  radiance = color(0, 0, 0);
  double T = 1.0;
  for (const hit &h : hits) {
    const gaussian &g = g_[static_cast<size_t>(h.index)];
    radiance += (T * h.alpha) * eval_sh(degree_, g.sh, d);
    T *= 1.0 - h.alpha;
  }
  transmittance = T;
}

double gaussian_field::transmittance(const ray &r, double t_min, double t_max) const {
  static thread_local std::vector<hit> hits;
  point3 o;
  vec3 d;
  to_local(r, o, d);
  collect(o, d, t_min, t_max, hits);
  double T = 1.0;
  for (const hit &h : hits)
    T *= 1.0 - h.alpha;
  return T;
}

aabb gaussian_field::world_bounds() const {
  if (nodes_.empty())
    return aabb(position_, position_);
  const node &root = nodes_[0];
  point3 lo(INFINITY, INFINITY, INFINITY), hi(-INFINITY, -INFINITY, -INFINITY);
  for (int c = 0; c < 8; ++c) {
    const double l[3] = {(c & 1) ? root.hi[0] : root.lo[0], (c & 2) ? root.hi[1] : root.lo[1],
                         (c & 4) ? root.hi[2] : root.lo[2]};
    // x_world = position + scale * R l, with R = rot_^T.
    double w[3];
    for (int i = 0; i < 3; ++i)
      w[i] = position_[i] + scale_ * (rot_[i] * l[0] + rot_[3 + i] * l[1] + rot_[6 + i] * l[2]);
    lo = point3(std::min(lo.x(), w[0]), std::min(lo.y(), w[1]), std::min(lo.z(), w[2]));
    hi = point3(std::max(hi.x(), w[0]), std::max(hi.y(), w[1]), std::max(hi.z(), w[2]));
  }
  return aabb(lo, hi);
}
