#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "vec2.hpp"

enum class ShapeType { Circle, Polygon };

struct ShapeMassData {
    double area;
    double inertiaOverMass;
};

class Shape {
public:
    static Shape makeCircle(double radius) {
        Shape s;
        s.type_ = ShapeType::Circle;
        s.radius_ = radius;
        s.conservativeRadius_ = radius;
        s.massData_.area = M_PI * radius * radius;
        s.massData_.inertiaOverMass = 0.5 * radius * radius;
        return s;
    }

    static Shape makePolygon(std::vector<Vec2> points) {
        Shape s;
        s.type_ = ShapeType::Polygon;
        s.buildPolygon(std::move(points));
        return s;
    }

    ShapeType type() const { return type_; }
    double radius() const { return radius_; }
    const std::vector<Vec2>& localVertices() const { return vertices_; }
    const std::vector<Vec2>& localNormals() const { return normals_; }
    double conservativeRadius() const { return conservativeRadius_; }
    const ShapeMassData& massData() const { return massData_; }

private:
    Shape() = default;

    void buildPolygon(std::vector<Vec2> points) {
        std::size_t n = points.size();
        double signedArea = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            signedArea += cross(points[i], points[(i + 1) % n]);
        }
        signedArea *= 0.5;
        if (signedArea < 0.0) {
            std::reverse(points.begin(), points.end());
            signedArea = -signedArea;
        }

        Vec2 centroidRaw(0.0, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2& p0 = points[i];
            const Vec2& p1 = points[(i + 1) % n];
            double cr = cross(p0, p1);
            centroidRaw += (p0 + p1) * cr;
        }
        Vec2 centroid = centroidRaw * (1.0 / (6.0 * signedArea));

        vertices_.reserve(n);
        for (const auto& p : points) {
            vertices_.push_back(p - centroid);
        }

        normals_.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            Vec2 edge = vertices_[(i + 1) % n] - vertices_[i];
            normals_.push_back(normalized(Vec2(edge.y, -edge.x)));
        }

        double numer = 0.0;
        double denom = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2& p0 = vertices_[i];
            const Vec2& p1 = vertices_[(i + 1) % n];
            double cr = std::fabs(cross(p0, p1));
            double intx2 = p0.x * p0.x + p0.x * p1.x + p1.x * p1.x;
            double inty2 = p0.y * p0.y + p0.y * p1.y + p1.y * p1.y;
            numer += cr * (intx2 + inty2);
            denom += cr;
        }

        massData_.area = signedArea;
        massData_.inertiaOverMass = numer / (6.0 * signedArea);

        double minDist = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < n; ++i) {
            minDist = std::min(minDist, dot(normals_[i], vertices_[i]));
        }
        conservativeRadius_ = minDist;
        (void)denom;
    }

    ShapeType type_ = ShapeType::Circle;
    double radius_ = 0.0;
    std::vector<Vec2> vertices_;
    std::vector<Vec2> normals_;
    ShapeMassData massData_{0.0, 0.0};
    double conservativeRadius_ = 0.0;
};

class Body {
public:
    Vec2 position;
    double angle = 0.0;
    Vec2 linearVelocity;
    double angularVelocity = 0.0;
    double invMass = 0.0;
    double invInertia = 0.0;
    double restitution = 0.2;
    double friction = 0.3;
    Shape shape;

    Body(const Vec2& positionIn, Shape shapeIn, double density, bool isStatic)
        : position(positionIn), shape(std::move(shapeIn)) {
        if (isStatic) {
            invMass = 0.0;
            invInertia = 0.0;
        } else {
            double mass = density * shape.massData().area;
            double inertia = mass * shape.massData().inertiaOverMass;
            invMass = mass > 1e-12 ? 1.0 / mass : 0.0;
            invInertia = inertia > 1e-12 ? 1.0 / inertia : 0.0;
        }
    }

    bool isStatic() const { return invMass == 0.0 && invInertia == 0.0; }

    Vec2 worldVertex(std::size_t i) const {
        return position + rotate(shape.localVertices()[i], angle);
    }

    Vec2 worldNormal(std::size_t i) const {
        return rotate(shape.localNormals()[i], angle);
    }

    Vec2 velocityAtPoint(const Vec2& worldPoint) const {
        Vec2 r = worldPoint - position;
        return linearVelocity + cross(angularVelocity, r);
    }
};
