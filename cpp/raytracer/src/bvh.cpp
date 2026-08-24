#include "bvh.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace rt {

namespace {

AABB boxOf(const Hittable& h) {
    AABB box;
    if (!h.boundingBox(box)) {
        // Every primitive in this project (currently just Sphere) is
        // finite, so this should be unreachable; it's here so a future
        // unbounded primitive fails loudly instead of corrupting the tree.
        throw std::logic_error("BVH: encountered an object with no bounding box");
    }
    return box;
}

Point3 centroid(const AABB& box) { return 0.5 * (box.min() + box.max()); }

// Picks the axis along which the objects' centroids are most spread out.
// Splitting there (rather than always alternating x/y/z by tree depth)
// tends to produce more balanced subtrees with tighter bounding boxes,
// which means fewer wasted box tests per ray at query time.
int chooseSplitAxis(const std::vector<std::unique_ptr<Hittable>>& objects) {
    double lo[3] = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::infinity()};
    double hi[3] = {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                     -std::numeric_limits<double>::infinity()};

    for (const auto& obj : objects) {
        Point3 c = centroid(boxOf(*obj));
        double comps[3] = {c.x, c.y, c.z};
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = std::min(lo[axis], comps[axis]);
            hi[axis] = std::max(hi[axis], comps[axis]);
        }
    }

    int bestAxis = 0;
    double bestExtent = hi[0] - lo[0];
    for (int axis = 1; axis < 3; ++axis) {
        double extent = hi[axis] - lo[axis];
        if (extent > bestExtent) {
            bestExtent = extent;
            bestAxis = axis;
        }
    }
    return bestAxis;
}

}  // namespace

std::unique_ptr<Hittable> BVHNode::build(std::vector<std::unique_ptr<Hittable>> objects) {
    if (objects.empty()) {
        throw std::invalid_argument("BVHNode::build: cannot build a BVH over zero objects");
    }
    if (objects.size() == 1) {
        return std::move(objects.front());
    }
    if (objects.size() == 2) {
        auto node = std::unique_ptr<BVHNode>(new BVHNode());
        node->left_ = std::move(objects[0]);
        node->right_ = std::move(objects[1]);
        node->box_ = surroundingBox(boxOf(*node->left_), boxOf(*node->right_));
        return node;
    }

    int axis = chooseSplitAxis(objects);
    std::sort(objects.begin(), objects.end(),
              [axis](const std::unique_ptr<Hittable>& a, const std::unique_ptr<Hittable>& b) {
                  double ca = AABB::component(centroid(boxOf(*a)), axis);
                  double cb = AABB::component(centroid(boxOf(*b)), axis);
                  return ca < cb;
              });

    // Median split: after sorting by centroid along the chosen axis, put
    // the first half in one child and the second half in the other. This
    // isn't as tight as a full surface-area-heuristic split, but it's
    // simple, guarantees a balanced (O(log n) depth) tree, and is more
    // than sufficient for the few hundred objects this project renders.
    size_t mid = objects.size() / 2;
    std::vector<std::unique_ptr<Hittable>> leftObjects(std::make_move_iterator(objects.begin()),
                                                         std::make_move_iterator(objects.begin() + mid));
    std::vector<std::unique_ptr<Hittable>> rightObjects(std::make_move_iterator(objects.begin() + mid),
                                                          std::make_move_iterator(objects.end()));

    auto node = std::unique_ptr<BVHNode>(new BVHNode());
    node->left_ = build(std::move(leftObjects));
    node->right_ = build(std::move(rightObjects));
    node->box_ = surroundingBox(boxOf(*node->left_), boxOf(*node->right_));
    return node;
}

bool BVHNode::hit(const Ray& r, double tMin, double tMax, HitRecord& rec) const {
    if (!box_.hit(r, tMin, tMax)) {
        return false;
    }

    bool hitLeft = left_ && left_->hit(r, tMin, tMax, rec);
    // Once the left subtree reports a hit at rec.t, shrink tMax to rec.t
    // before testing the right subtree so it can only report a *closer*
    // hit; this is what keeps the traversal correct without needing to
    // compare and pick the nearer of two independent HitRecords.
    bool hitRight = right_ && right_->hit(r, tMin, hitLeft ? rec.t : tMax, rec);

    return hitLeft || hitRight;
}

bool BVHNode::boundingBox(AABB& outBox) const {
    outBox = box_;
    return true;
}

}  // namespace rt
