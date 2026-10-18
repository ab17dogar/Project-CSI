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

void gaussian_field::sh_basis(const vec3 &dir, double Y[16]) {
  const double x = dir.x(), y = dir.y(), z = dir.z();
  const double xx = x * x, yy = y * y, zz = z * z, xy = x * y, yz = y * z, xz = x * z;
  Y[0] = kC0;
  Y[1] = -kC1 * y;
  Y[2] = kC1 * z;
  Y[3] = -kC1 * x;
  Y[4] = kC2[0] * xy;
  Y[5] = kC2[1] * yz;
  Y[6] = kC2[2] * (2 * zz - xx - yy);
  Y[7] = kC2[3] * xz;
  Y[8] = kC2[4] * (xx - yy);
  Y[9] = kC3[0] * y * (3 * xx - yy);
  Y[10] = kC3[1] * xy * z;
  Y[11] = kC3[2] * y * (4 * zz - xx - yy);
  Y[12] = kC3[3] * z * (2 * zz - 3 * xx - 3 * yy);
  Y[13] = kC3[4] * x * (4 * zz - xx - yy);
  Y[14] = kC3[5] * z * (xx - yy);
  Y[15] = kC3[6] * x * (xx - 3 * yy);
}

bool gaussian_field::parse_selection(const std::string &name, selection &out) {
  if (name == "power") out = selection::power;
  else if (name == "tree") out = selection::tree;
  else if (name == "tree-distance") out = selection::tree_distance;
  else if (name == "tree-emitter") out = selection::tree_emitter;
  else return false;
  return true;
}

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

  // Emitted power per Gaussian in the particle model: through a plane normal
  // to w it lets out o L times its projected area 2 pi sqrt(det Sigma_2D(w)),
  // with sqrt(det Sigma_2D(w)) = s1 s2 s3 |M w|. Integrated over w with
  // |M w| replaced by its RMS over the sphere (exact for spheres, within 15%
  // for flat discs; it only steers selection, never the estimate):
  //   Phi = 8 pi^2 o L s1 s2 s3 sqrt((s1^-2 + s2^-2 + s3^-2) / 3).
  std::vector<double> power(g_.size());
  total_power_ = 0.0;
  for (size_t i = 0; i < g_.size(); ++i) {
    const gaussian &g = g_[i];
    double inv_s2[3], prod = 1.0;
    for (int k = 0; k < 3; ++k) {
      inv_s2[k] = 0.0;
      for (int j = 0; j < 3; ++j)
        inv_s2[k] += static_cast<double>(g.whiten[k * 3 + j]) * g.whiten[k * 3 + j];
      prod /= std::sqrt(inv_s2[k]); // rows of M have length 1 / s_k
    }
    color dc;
    for (int c = 0; c < 3; ++c)
      dc[c] = std::max(0.0, kC0 * g.sh[c] + 0.5);
    power[i] = 8.0 * PI * PI * g.opacity * luminance(dc) * prod *
               std::sqrt((inv_s2[0] + inv_s2[1] + inv_s2[2]) / 3.0);
    total_power_ += power[i];
  }
  if (!power.empty())
    select_.build(power);
  total_power_ *= scale_ * scale_; // areas scale with the transform

  // Light hierarchy on the same nodes: power sums, parents, leaves.
  g_power_ = power;
  node_power_.assign(nodes_.size(), 0.0);
  parent_.assign(nodes_.size(), -1);
  leaf_of_.assign(g_.size(), -1);
  for (int k = static_cast<int>(nodes_.size()) - 1; k >= 0; --k) {
    const node &n = nodes_[static_cast<size_t>(k)];
    if (n.count > 0) {
      for (int i = n.first; i < n.first + n.count; ++i) {
        const int g = order_[static_cast<size_t>(i)];
        node_power_[static_cast<size_t>(k)] += power[static_cast<size_t>(g)];
        leaf_of_[static_cast<size_t>(g)] = k;
      }
    } else {
      // Children have larger indices, so their sums are complete.
      parent_[static_cast<size_t>(k + 1)] = k;
      parent_[static_cast<size_t>(n.first)] = k;
      node_power_[static_cast<size_t>(k)] =
          node_power_[static_cast<size_t>(k + 1)] + node_power_[static_cast<size_t>(n.first)];
    }
  }

  // Orientation and SH aggregates (stage 2c), bottom-up like the power.
  const size_t N = g_.size(), K = nodes_.size();
  g_lum_dc_.assign(N, 0.0f);
  g_det_.assign(N, 0.0f);
  node_w_.assign(K, 0.0f);
  node_q_.assign(K * 6, 0.0f);
  node_shw_.assign(K, 0.0f);
  node_sh_.assign(degree_ > 0 ? K * 16 : 0, 0.0f);
  const int nb = (degree_ + 1) * (degree_ + 1);
  g_lum_sh_.assign(degree_ > 0 ? N * static_cast<size_t>(nb) : 0, 0.0f);
  std::vector<double> gq(N * 6), gsh(degree_ > 0 ? N * 16 : 0), gshw(N);
  for (size_t i = 0; i < N; ++i) {
    const gaussian &g = g_[i];
    const float *M = g.whiten;
    const double det = std::abs(M[0] * (M[4] * M[8] - M[5] * M[7]) - M[1] * (M[3] * M[8] - M[5] * M[6]) +
                                M[2] * (M[3] * M[7] - M[4] * M[6]));
    color dc;
    for (int c = 0; c < 3; ++c)
      dc[c] = std::max(0.0, kC0 * g.sh[c] + 0.5);
    const double lum = luminance(dc), w = g.opacity * lum;
    g_lum_dc_[i] = static_cast<float>(lum);
    g_det_[i] = static_cast<float>(det);
    const double s = (2.0 * PI / det) * (2.0 * PI / det) * w;
    const int idx[6][2] = {{0, 0}, {0, 1}, {0, 2}, {1, 1}, {1, 2}, {2, 2}};
    for (int e = 0; e < 6; ++e) {
      double mtm = 0.0; // (M^T M)_ab = sum_r M_ra M_rb
      for (int r = 0; r < 3; ++r)
        mtm += static_cast<double>(M[r * 3 + idx[e][0]]) * M[r * 3 + idx[e][1]];
      gq[i * 6 + e] = s * mtm;
    }
    // SH weights: opacity times mean projected area, Phi_i / (4 pi L_i).
    gshw[i] = lum > 0.0 ? power[i] / (4.0 * PI * lum) : 0.0;
    if (degree_ > 0)
      for (int k = 0; k < 16; ++k) {
        const double l = luminance(color(g.sh[k * 3], g.sh[k * 3 + 1], g.sh[k * 3 + 2]));
        gsh[i * 16 + k] = gshw[i] * l;
        if (k < nb)
          g_lum_sh_[i * static_cast<size_t>(nb) + static_cast<size_t>(k)] = static_cast<float>(l);
      }
  }
  for (int k = static_cast<int>(K) - 1; k >= 0; --k) {
    const node &n = nodes_[static_cast<size_t>(k)];
    const size_t kk = static_cast<size_t>(k);
    auto accumulate = [&](auto &&add) {
      if (n.count > 0) {
        for (int i = n.first; i < n.first + n.count; ++i)
          add(static_cast<size_t>(order_[static_cast<size_t>(i)]), true);
      } else {
        add(static_cast<size_t>(k + 1), false);
        add(static_cast<size_t>(n.first), false);
      }
    };
    double W = 0, shw = 0, q[6] = {0, 0, 0, 0, 0, 0}, sh[16] = {};
    accumulate([&](size_t j, bool gaussian_index) {
      if (gaussian_index) {
        W += g_[j].opacity * g_lum_dc_[j];
        shw += gshw[j];
        for (int e = 0; e < 6; ++e) q[e] += gq[j * 6 + e];
        if (degree_ > 0) for (int c = 0; c < 16; ++c) sh[c] += gsh[j * 16 + c];
      } else {
        W += node_w_[j];
        shw += node_shw_[j];
        for (int e = 0; e < 6; ++e) q[e] += node_q_[j * 6 + e];
        if (degree_ > 0) for (int c = 0; c < 16; ++c) sh[c] += node_sh_[j * 16 + c];
      }
    });
    node_w_[kk] = static_cast<float>(W);
    node_shw_[kk] = static_cast<float>(shw);
    for (int e = 0; e < 6; ++e) node_q_[kk * 6 + e] = static_cast<float>(q[e]);
    if (degree_ > 0) for (int c = 0; c < 16; ++c) node_sh_[kk * 16 + c] = static_cast<float>(sh[c]);
  }
}

point3 gaussian_field::point_to_local(const point3 &x) const {
  const vec3 a = (x - position_) / scale_;
  return point3(rot_[0] * a[0] + rot_[1] * a[1] + rot_[2] * a[2],
                rot_[3] * a[0] + rot_[4] * a[1] + rot_[5] * a[2],
                rot_[6] * a[0] + rot_[7] * a[1] + rot_[8] * a[2]);
}

vec3 gaussian_field::normal_to_local(const vec3 &n) const {
  return vec3(rot_[0] * n[0] + rot_[1] * n[1] + rot_[2] * n[2],
              rot_[3] * n[0] + rot_[4] * n[1] + rot_[5] * n[2],
              rot_[6] * n[0] + rot_[7] * n[1] + rot_[8] * n[2]);
}

namespace {

// Largest cos(angle to n) over a box (centre c, half-extents h) seen from
// x: 0 if the whole box lies below the tangent plane, else the bound of its
// bounding sphere, cos(max(0, theta_c - theta_b)), theta_b the sphere's
// angular radius. 1 if n is zero (no horizon) or x is inside the sphere.
// The plane test is exact for axis-aligned normals; the sphere alone is not,
// and a node whose sphere crosses the plane while its children's boxes lie
// below it would waste its samples.
// With dn = n.(c - x) and d^2 = |c - x|^2: cos_c = dn / d, cos_b =
// sqrt(d^2 - r^2) / d, so cos_c >= cos_b iff dn >= 0 and dn^2 >= d^2 - r^2,
// and cos_c cos_b + sin_c sin_b = (dn sqrt(d^2 - r^2) + sqrt((d^2 - dn^2) r^2)) / d^2.
inline double cosine_bound(const vec3 &n, const point3 &x, const point3 &c, const vec3 &h) {
  if (n.x() == 0.0 && n.y() == 0.0 && n.z() == 0.0)
    return 1.0;
  const vec3 v = c - x;
  const double dn = dot(n, v);
  if (dn + std::abs(n.x()) * h.x() + std::abs(n.y()) * h.y() + std::abs(n.z()) * h.z() <= 0.0)
    return 0.0;
  const double r2 = h.length_squared();
  const double d2 = v.length_squared();
  if (d2 <= r2 || (dn >= 0.0 && dn * dn >= d2 - r2))
    return 1.0;
  const double b = dn * std::sqrt(d2 - r2) + std::sqrt(std::max(0.0, d2 - dn * dn) * r2);
  return std::max(0.0, b / d2);
}

// The SH basis up to the given degree (Y[k] for k < (degree + 1)^2).
inline void sh_basis_to(int degree, const vec3 &dir, double Y[16]) {
  if (degree >= 2) {
    gaussian_field::sh_basis(dir, Y);
    return;
  }
  Y[0] = kC0;
  if (degree == 1) {
    Y[1] = -kC1 * dir.y();
    Y[2] = kC1 * dir.z();
    Y[3] = -kC1 * dir.x();
  }
}

// Branch probabilities of the light hierarchy, reused along one ray.
struct branch_memo {
  const void *owner = nullptr;
  uint32_t gen = 0;
  std::vector<uint32_t> stamp;
  std::vector<double> prob;
};
thread_local branch_memo t_memo;

} // namespace

void gaussian_field::memo_begin() const {
  if (t_memo.owner != this || t_memo.stamp.size() != nodes_.size()) {
    t_memo.owner = this;
    t_memo.stamp.assign(nodes_.size(), 0);
    t_memo.prob.assign(nodes_.size(), 0.0);
    t_memo.gen = 0;
  }
  if (++t_memo.gen == 0) { // wrapped: clear stamps
    std::fill(t_memo.stamp.begin(), t_memo.stamp.end(), 0);
    t_memo.gen = 1;
  }
}

// A heuristic estimate of node k's contribution at x; it only has to be the
// same in select_local and select_pmf_local. Distance: clamped below by the
// node's half-diagonal. Emitter (tree_emitter, tree): far nodes (beyond twice
// their half-diagonal) by the Cauchy-Schwarz bound sqrt(W w^T Q w) on their
// Gaussians' projected areas toward x, times their power-weighted SH
// radiance toward x over the view-independent one; near nodes by their
// direction-averaged emission Phi / 4 pi. Receiver (tree): the cosine bound.
double gaussian_field::node_importance(int k, const point3 &x, const vec3 &n) const {
  const node &nd = nodes_[static_cast<size_t>(k)];
  const size_t kk = static_cast<size_t>(k);
  point3 c;
  vec3 h;
  for (int a = 0; a < 3; ++a) {
    c[a] = 0.5 * (nd.lo[a] + nd.hi[a]);
    h[a] = 0.5 * (nd.hi[a] - nd.lo[a]);
  }
  const double r2 = h.length_squared();
  const vec3 v = c - x;
  const double d2 = v.length_squared();
  if (selection_ == selection::tree_distance)
    return node_power_[kk] / std::max(d2, r2);
  double e = node_power_[kk] / (4.0 * PI);
  if (d2 > 4.0 * r2) {
    const vec3 w = v / std::sqrt(d2);
    const float *q = &node_q_[kk * 6];
    const double quad = q[0] * w.x() * w.x() + q[3] * w.y() * w.y() + q[5] * w.z() * w.z() +
                        2.0 * (q[1] * w.x() * w.y() + q[2] * w.x() * w.z() + q[4] * w.y() * w.z());
    e = std::sqrt(std::max(0.0, node_w_[kk] * quad));
    if (degree_ > 0 && node_shw_[kk] > 0.0f) {
      double Y[16];
      sh_basis_to(degree_, w, Y);
      const float *sh = &node_sh_[kk * 16];
      const int nb = (degree_ + 1) * (degree_ + 1);
      double dir = 0.0;
      for (int b = 0; b < nb; ++b)
        dir += Y[b] * sh[b];
      const double base = kC0 * sh[0] / node_shw_[kk] + 0.5;
      const double toward = dir / node_shw_[kk] + 0.5;
      if (base > 0.0)
        e *= std::max(toward, 0.1 * base) / base;
    }
  }
  double imp = e / std::max(d2, r2);
  if (selection_ == selection::tree)
    imp *= cosine_bound(n, x, c, h);
  return imp;
}

// Gaussian g's estimated contribution at x: with the emitter terms, its
// exact projected area 2 pi |M w| / |det M| and the luminance of its SH
// radiance toward x (linear in the SH, so before the per-channel clamp;
// floored at a tenth of the view-independent one, so view-dependent
// Gaussians are not ruled out by one direction), over the clamped squared
// distance; inside its support, its direction-averaged emission.
double gaussian_field::leaf_weight(int gi, const point3 &x, const vec3 &n) const {
  const size_t g = static_cast<size_t>(gi);
  const gaussian &G = g_[g];
  point3 m;
  vec3 h;
  for (int a = 0; a < 3; ++a) {
    m[a] = G.mean[a];
    h[a] = 0.5 * (box_hi_[g * 3 + a] - box_lo_[g * 3 + a]);
  }
  const double r2 = h.length_squared();
  const vec3 v = m - x;
  const double d2 = v.length_squared();
  if (selection_ == selection::tree_distance)
    return g_power_[g] / std::max(d2, r2);
  double e = g_power_[g] / (4.0 * PI);
  if (d2 > r2) {
    const vec3 w = v / std::sqrt(d2);
    const float *M = G.whiten;
    double mw2 = 0.0;
    for (int r = 0; r < 3; ++r) {
      const double row = M[r * 3] * w.x() + M[r * 3 + 1] * w.y() + M[r * 3 + 2] * w.z();
      mw2 += row * row;
    }
    const double area = 2.0 * PI * std::sqrt(mw2) / g_det_[g];
    const double lum_dc = g_lum_dc_[g];
    double lum = lum_dc;
    if (degree_ > 0) {
      double Y[16];
      sh_basis_to(degree_, w, Y);
      const int nb = (degree_ + 1) * (degree_ + 1);
      const float *ls = &g_lum_sh_[g * static_cast<size_t>(nb)];
      lum = 0.5;
      for (int b = 0; b < nb; ++b)
        lum += Y[b] * ls[b];
    }
    e = G.opacity * area * std::max(lum, 0.1 * lum_dc);
  }
  double imp = e / std::max(d2, r2);
  if (selection_ == selection::tree)
    imp *= cosine_bound(n, x, m, h);
  return imp;
}

int gaussian_field::select_local(const point3 &x, const vec3 &n, double u, double &pmf) const {
  pmf = 0.0;
  if (g_.empty() || !(total_power_ > 0.0))
    return -1;
  if (selection_ == selection::power) {
    size_t i = select_.sample_discrete(u, pmf);
    return static_cast<int>(i);
  }
  int k = 0;
  double p = 1.0;
  while (nodes_[static_cast<size_t>(k)].count == 0) {
    const int left = k + 1, right = nodes_[static_cast<size_t>(k)].first;
    const double il = node_importance(left, x, n), ir = node_importance(right, x, n);
    if (!(il + ir > 0.0))
      return -1;
    const double pl = il / (il + ir);
    if (u < pl) {
      k = left;
      u = u / pl;
      p *= pl;
    } else {
      k = right;
      u = (u - pl) / (1.0 - pl);
      p *= 1.0 - pl;
    }
    u = std::min(u, ONE_MINUS_EPSILON);
  }
  const node &leaf = nodes_[static_cast<size_t>(k)];
  // Leaves hold up to 4 Gaussians unless their centroids coincide.
  double weights[16];
  const bool cached = leaf.count <= 16;
  double total = 0.0;
  for (int i = leaf.first; i < leaf.first + leaf.count; ++i) {
    const double w = leaf_weight(order_[static_cast<size_t>(i)], x, n);
    if (cached)
      weights[i - leaf.first] = w;
    total += w;
  }
  if (!(total > 0.0))
    return -1;
  // The last Gaussian with positive weight absorbs round-off in u.
  double acc = 0.0, chosen_w = 0.0;
  int chosen = -1;
  for (int i = leaf.first; i < leaf.first + leaf.count; ++i) {
    const int g = order_[static_cast<size_t>(i)];
    const double w = cached ? weights[i - leaf.first] : leaf_weight(g, x, n);
    if (w <= 0.0)
      continue;
    chosen = g;
    chosen_w = w;
    acc += w / total;
    if (u < acc)
      break;
  }
  pmf = p * chosen_w / total;
  return chosen;
}

double gaussian_field::select_pmf_local(int i, const point3 &x, const vec3 &n, bool memo) const {
  if (i < 0 || static_cast<size_t>(i) >= g_.size() || !(total_power_ > 0.0))
    return 0.0;
  if (selection_ == selection::power)
    return select_.discrete_pmf(static_cast<size_t>(i));
  int k = leaf_of_[static_cast<size_t>(i)];
  const node &leaf = nodes_[static_cast<size_t>(k)];
  double total = 0.0, own = 0.0;
  for (int j = leaf.first; j < leaf.first + leaf.count; ++j) {
    const int g = order_[static_cast<size_t>(j)];
    const double w = leaf_weight(g, x, n);
    total += w;
    if (g == i)
      own = w;
  }
  if (!(total > 0.0))
    return 0.0;
  double p = own / total;
  while (parent_[static_cast<size_t>(k)] >= 0) {
    const size_t kk = static_cast<size_t>(k);
    double branch;
    if (memo && t_memo.stamp[kk] == t_memo.gen) {
      branch = t_memo.prob[kk];
    } else {
      const int par = parent_[kk];
      const int left = par + 1, right = nodes_[static_cast<size_t>(par)].first;
      const double il = node_importance(left, x, n), ir = node_importance(right, x, n);
      branch = il + ir > 0.0 ? (k == left ? il : ir) / (il + ir) : 0.0;
      if (memo) {
        t_memo.stamp[kk] = t_memo.gen;
        t_memo.prob[kk] = branch;
      }
    }
    if (branch == 0.0)
      return 0.0;
    p *= branch;
    k = parent_[kk];
  }
  return p;
}

int gaussian_field::select_gaussian(const point3 &x, double u, double &pmf, const vec3 &n) const {
  return select_local(point_to_local(x), normal_to_local(n), u, pmf);
}

double gaussian_field::select_pmf(int i, const point3 &x, const vec3 &n) const {
  return select_pmf_local(i, point_to_local(x), normal_to_local(n));
}

double gaussian_field::direction_pdf_local(const gaussian &g, const point3 &o,
                                           const vec3 &w) const {
  // a = M (o - mu), b = M w: along the ray the whitened distance is
  // |a + t b|^2 = beta^2 (t - t*)^2 + d^2. With u = beta (t - t*),
  //   p = (2 pi)^-3/2 |det M| e^{-d^2/2} / beta
  //       * [ (t*^2 + 1/beta^2) I0 + (t*/beta) e^{-u0^2/2} ],
  // u0 = -beta t*, I0 = integral_{u0}^inf e^{-u^2/2} du.
  double a[3], b[3], M[9];
  for (int k = 0; k < 9; ++k)
    M[k] = g.whiten[k];
  const double rel[3] = {o.x() - g.mean[0], o.y() - g.mean[1], o.z() - g.mean[2]};
  for (int r = 0; r < 3; ++r) {
    a[r] = M[r * 3] * rel[0] + M[r * 3 + 1] * rel[1] + M[r * 3 + 2] * rel[2];
    b[r] = M[r * 3] * w.x() + M[r * 3 + 1] * w.y() + M[r * 3 + 2] * w.z();
  }
  const double bb = b[0] * b[0] + b[1] * b[1] + b[2] * b[2];
  if (!(bb > 0.0))
    return 0.0;
  const double beta = std::sqrt(bb);
  const double ts = -(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / bb;
  const double p0 = a[0] + ts * b[0], p1 = a[1] + ts * b[1], p2 = a[2] + ts * b[2];
  const double d2 = p0 * p0 + p1 * p1 + p2 * p2;
  const double det = std::abs(M[0] * (M[4] * M[8] - M[5] * M[7]) -
                              M[1] * (M[3] * M[8] - M[5] * M[6]) +
                              M[2] * (M[3] * M[7] - M[4] * M[6]));
  const double u0 = -beta * ts;
  const double I0 = std::sqrt(PI / 2.0) * std::erfc(u0 / std::sqrt(2.0));
  const double bracket = (ts * ts + 1.0 / bb) * I0 + (ts / beta) * std::exp(-0.5 * u0 * u0);
  const double p = std::pow(2.0 * PI, -1.5) * det * std::exp(-0.5 * d2) / beta * bracket;
  return p > 0.0 ? p : 0.0; // round-off when the Gaussian is far behind
}

double gaussian_field::emission_pdf(int i, const point3 &x, const vec3 &w, const vec3 &n) const {
  if (i < 0 || static_cast<size_t>(i) >= g_.size())
    return 0.0;
  point3 o;
  vec3 d;
  to_local(ray(x, w), o, d);
  return select_pmf_local(i, o, normal_to_local(n)) *
         direction_pdf_local(g_[static_cast<size_t>(i)], o, unit_vector(d));
}

bool gaussian_field::sample_emission(const point3 &x, const vec3 &n, double u_select,
                                     const double u[4], light_sample &out) const {
  if (g_.empty() || !(total_power_ > 0.0))
    return false;
  double pmf = 0.0;
  const int selected = select_local(point_to_local(x), normal_to_local(n), u_select, pmf);
  if (selected < 0 || !(pmf > 0.0))
    return false;
  const size_t i = static_cast<size_t>(selected);
  const gaussian &g = g_[i];
  // Three standard normals z (Box-Muller), then y = mu + M^-1 z.
  const double r0 = std::sqrt(-2.0 * std::log(1.0 - u[0])), a0 = 2.0 * PI * u[1];
  const double r1 = std::sqrt(-2.0 * std::log(1.0 - u[2])), a1 = 2.0 * PI * u[3];
  const double z[3] = {r0 * std::cos(a0), r0 * std::sin(a0), r1 * std::cos(a1)};
  double M[9];
  for (int k = 0; k < 9; ++k)
    M[k] = g.whiten[k];
  const double c00 = M[4] * M[8] - M[5] * M[7], c01 = M[2] * M[7] - M[1] * M[8],
               c02 = M[1] * M[5] - M[2] * M[4];
  const double det = M[0] * c00 + M[3] * c01 + M[6] * c02;
  if (det == 0.0)
    return false;
  const double inv[9] = {c00 / det, c01 / det, c02 / det,
                         (M[5] * M[6] - M[3] * M[8]) / det, (M[0] * M[8] - M[2] * M[6]) / det,
                         (M[2] * M[3] - M[0] * M[5]) / det,
                         (M[3] * M[7] - M[4] * M[6]) / det, (M[1] * M[6] - M[0] * M[7]) / det,
                         (M[0] * M[4] - M[1] * M[3]) / det};
  double yl[3];
  for (int r = 0; r < 3; ++r)
    yl[r] = g.mean[r] + inv[r * 3] * z[0] + inv[r * 3 + 1] * z[1] + inv[r * 3 + 2] * z[2];
  // Local -> world: position + scale * R y, with R = rot_^T.
  point3 y;
  for (int r = 0; r < 3; ++r)
    y[r] = position_[r] + scale_ * (rot_[r] * yl[0] + rot_[3 + r] * yl[1] + rot_[6 + r] * yl[2]);
  vec3 w = y - x;
  const double len = w.length();
  if (!(len > 0.0))
    return false;
  w /= len;

  // The term this direction estimates: Gaussian i at its maximum response.
  point3 o;
  vec3 d;
  to_local(ray(x, w), o, d);
  double ol[3], dl[3];
  const double rel[3] = {o.x() - g.mean[0], o.y() - g.mean[1], o.z() - g.mean[2]};
  for (int r = 0; r < 3; ++r) {
    ol[r] = M[r * 3] * rel[0] + M[r * 3 + 1] * rel[1] + M[r * 3 + 2] * rel[2];
    dl[r] = M[r * 3] * d.x() + M[r * 3 + 1] * d.y() + M[r * 3 + 2] * d.z();
  }
  const double dd = dl[0] * dl[0] + dl[1] * dl[1] + dl[2] * dl[2];
  const double t = -(ol[0] * dl[0] + ol[1] * dl[1] + ol[2] * dl[2]) / dd;
  if (!(t > 0.0))
    return false;
  const double p0 = ol[0] + t * dl[0], p1 = ol[1] + t * dl[1], p2 = ol[2] + t * dl[2];
  const double alpha = g.opacity * std::exp(-0.5 * (p0 * p0 + p1 * p1 + p2 * p2));
  if (alpha < kMinAlpha)
    return false;
  const double pdf = pmf * direction_pdf_local(g, o, unit_vector(d));
  if (!(pdf > 0.0))
    return false;
  out.wi = w;
  out.dist = t;
  out.p = x + t * w;
  out.n = vec3(0, 0, 0);
  out.li = alpha * eval_sh(degree_, g.sh, d);
  out.pdf = pdf;
  out.is_delta = false;
  out.field_index = static_cast<int>(i);
  return true;
}

bool gaussian_field_light::sample_li(const point3 &ref, double u1, double u2,
                                     light_sample &out) const {
  return sample_li_at(ref, vec3(0, 0, 0), u1, u2, out);
}

bool gaussian_field_light::sample_li_at(const point3 &ref, const vec3 &n, double u1, double u2,
                                        light_sample &out) const {
  // Selection uses u1; the 3D point needs four more uniforms.
  const double u[4] = {u2, random_double(), random_double(), random_double()};
  return field_->sample_emission(ref, n, u1, u, out);
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
                           double &transmittance, const mis_context *mis) const {
  static thread_local std::vector<hit> hits;
  point3 o;
  vec3 d;
  to_local(r, o, d);
  collect(o, d, t_min, t_max, hits);
  std::sort(hits.begin(), hits.end(), [](const hit &a, const hit &b) {
    return a.t < b.t || (a.t == b.t && a.index < b.index);
  });
  // MIS: light densities are measured from the shading point.
  point3 mis_o;
  vec3 mis_w, mis_n;
  if (mis) {
    vec3 unused;
    to_local(ray(mis->origin, r.direction()), mis_o, unused);
    mis_w = unit_vector(d);
    mis_n = normal_to_local(mis->normal);
    if (selection_ != selection::power && !hits.empty())
      memo_begin();
  }
  radiance = color(0, 0, 0);
  double T = 1.0;
  for (const hit &h : hits) {
    const gaussian &g = g_[static_cast<size_t>(h.index)];
    double w = 1.0;
    if (mis) {
      const double light_pdf = mis->light_pmf * select_pmf_local(h.index, mis_o, mis_n, true) *
                               direction_pdf_local(g, mis_o, mis_w);
      if (mis->nee_only)
        w = light_pdf > 0.0 ? 0.0 : 1.0;
      else if (light_pdf > 0.0)
        w = sampling::power_heuristic(mis->bsdf_pdf, light_pdf);
    }
    if (w > 0.0)
      radiance += (w * T * h.alpha) * eval_sh(degree_, g.sh, d);
    T *= 1.0 - h.alpha;
  }
  transmittance = T;
}

double gaussian_field::transmittance(const ray &r, double t_min, double t_max,
                                     int exclude) const {
  static thread_local std::vector<hit> hits;
  point3 o;
  vec3 d;
  to_local(r, o, d);
  collect(o, d, t_min, t_max, hits);
  double T = 1.0;
  for (const hit &h : hits)
    if (h.index != exclude)
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
