#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "shapes.hpp"

struct Contact {
    Vec2 point;
    double penetration;
};

struct Manifold {
    Body* a = nullptr;
    Body* b = nullptr;
    Vec2 normal;
    std::vector<Contact> contacts;
};

namespace collision_detail {

inline bool circleVsCircle(Body& a, Body& b, Manifold& m) {
    Vec2 delta = b.position - a.position;
    double dist = length(delta);
    double radiusSum = a.shape.radius() + b.shape.radius();
    if (dist >= radiusSum) {
        return false;
    }
    Vec2 normal = dist > 1e-9 ? delta * (1.0 / dist) : Vec2(1.0, 0.0);
    double penetration = radiusSum - dist;
    Vec2 point = a.position + normal * a.shape.radius();
    m.normal = normal;
    m.contacts.push_back(Contact{point, penetration});
    return true;
}

struct AxisResult {
    double separation;
    int index;
};

inline AxisResult findMaxSeparation(const Body& refBody, const Body& otherBody) {
    const auto& normals = refBody.shape.localNormals();
    double bestSeparation = -std::numeric_limits<double>::max();
    int bestIndex = 0;
    std::size_t otherCount = otherBody.shape.localVertices().size();
    for (std::size_t i = 0; i < normals.size(); ++i) {
        Vec2 n = refBody.worldNormal(i);
        Vec2 v = refBody.worldVertex(i);
        double minProj = std::numeric_limits<double>::max();
        for (std::size_t j = 0; j < otherCount; ++j) {
            Vec2 p = otherBody.worldVertex(j);
            minProj = std::min(minProj, dot(n, p - v));
        }
        if (minProj > bestSeparation) {
            bestSeparation = minProj;
            bestIndex = static_cast<int>(i);
        }
    }
    return AxisResult{bestSeparation, bestIndex};
}

inline std::size_t findIncidentEdge(const Body& incBody, const Vec2& refNormalWorld) {
    const auto& normals = incBody.shape.localNormals();
    double minDot = std::numeric_limits<double>::max();
    std::size_t bestIndex = 0;
    for (std::size_t i = 0; i < normals.size(); ++i) {
        double d = dot(refNormalWorld, incBody.worldNormal(i));
        if (d < minDot) {
            minDot = d;
            bestIndex = i;
        }
    }
    return bestIndex;
}

inline int clipSegmentToLine(const Vec2 inPts[2], const double inPen[2], Vec2 outPts[2], double outPen[2],
                              const Vec2& normal, double offset) {
    int numOut = 0;
    double d0 = dot(normal, inPts[0]) - offset;
    double d1 = dot(normal, inPts[1]) - offset;
    if (d0 <= 0.0) {
        outPts[numOut] = inPts[0];
        outPen[numOut] = inPen[0];
        numOut++;
    }
    if (d1 <= 0.0) {
        outPts[numOut] = inPts[1];
        outPen[numOut] = inPen[1];
        numOut++;
    }
    if (d0 * d1 < 0.0) {
        double t = d0 / (d0 - d1);
        outPts[numOut] = inPts[0] + (inPts[1] - inPts[0]) * t;
        outPen[numOut] = inPen[0] + (inPen[1] - inPen[0]) * t;
        numOut++;
    }
    return numOut;
}

inline bool polygonVsPolygon(Body& a, Body& b, Manifold& m) {
    AxisResult sepA = findMaxSeparation(a, b);
    if (sepA.separation >= 0.0) {
        return false;
    }
    AxisResult sepB = findMaxSeparation(b, a);
    if (sepB.separation >= 0.0) {
        return false;
    }

    Body* refBody = nullptr;
    Body* incBody = nullptr;
    std::size_t refIndex = 0;
    bool flip = false;
    const double tol = 1e-3;
    if (sepB.separation > sepA.separation + tol) {
        refBody = &b;
        incBody = &a;
        refIndex = static_cast<std::size_t>(sepB.index);
        flip = true;
    } else {
        refBody = &a;
        incBody = &b;
        refIndex = static_cast<std::size_t>(sepA.index);
        flip = false;
    }

    Vec2 refNormal = refBody->worldNormal(refIndex);
    std::size_t incIndex = findIncidentEdge(*incBody, refNormal);
    std::size_t incNext = (incIndex + 1) % incBody->shape.localVertices().size();

    Vec2 incPts[2] = {incBody->worldVertex(incIndex), incBody->worldVertex(incNext)};
    double incPen[2] = {0.0, 0.0};

    std::size_t refNext = (refIndex + 1) % refBody->shape.localVertices().size();
    Vec2 v1 = refBody->worldVertex(refIndex);
    Vec2 v2 = refBody->worldVertex(refNext);
    Vec2 tangent = normalized(v2 - v1);

    double negSide = -dot(tangent, v1);
    double posSide = dot(tangent, v2);

    Vec2 clip1[2];
    double clip1Pen[2];
    int n1 = clipSegmentToLine(incPts, incPen, clip1, clip1Pen, Vec2(-tangent.x, -tangent.y), negSide);
    if (n1 < 2) {
        return false;
    }

    Vec2 clip2[2];
    double clip2Pen[2];
    int n2 = clipSegmentToLine(clip1, clip1Pen, clip2, clip2Pen, tangent, posSide);
    if (n2 < 2) {
        return false;
    }

    Vec2 finalNormal = flip ? Vec2(-refNormal.x, -refNormal.y) : refNormal;
    m.normal = finalNormal;

    double refOffset = dot(refNormal, v1);
    int contactsFound = 0;
    for (int i = 0; i < 2; ++i) {
        double separation = dot(refNormal, clip2[i]) - refOffset;
        if (separation <= 0.0) {
            m.contacts.push_back(Contact{clip2[i], -separation});
            contactsFound++;
        }
    }
    return contactsFound > 0;
}

inline bool circleVsPolygonRaw(const Body& circleBody, const Body& polyBody, Vec2& normalWorldOut, Vec2& contactWorldOut,
                                double& penetrationOut) {
    Vec2 centerLocal = rotate(circleBody.position - polyBody.position, -polyBody.angle);
    const auto& verts = polyBody.shape.localVertices();
    const auto& normals = polyBody.shape.localNormals();
    std::size_t n = verts.size();

    double maxSeparation = -std::numeric_limits<double>::max();
    std::size_t faceIndex = 0;
    for (std::size_t i = 0; i < n; ++i) {
        double s = dot(normals[i], centerLocal - verts[i]);
        if (s > circleBody.shape.radius()) {
            return false;
        }
        if (s > maxSeparation) {
            maxSeparation = s;
            faceIndex = i;
        }
    }

    Vec2 v1 = verts[faceIndex];
    Vec2 v2 = verts[(faceIndex + 1) % n];

    Vec2 normalLocal;
    Vec2 contactLocal;
    double penetration;

    if (maxSeparation < 1e-9) {
        normalLocal = normals[faceIndex];
        contactLocal = centerLocal - normalLocal * circleBody.shape.radius();
        penetration = circleBody.shape.radius() - maxSeparation;
    } else {
        double u1 = dot(centerLocal - v1, v2 - v1);
        double u2 = dot(centerLocal - v2, v1 - v2);
        Vec2 closest;
        if (u1 <= 0.0) {
            closest = v1;
        } else if (u2 <= 0.0) {
            closest = v2;
        } else {
            closest = v1 + (v2 - v1) * (u1 / lengthSquared(v2 - v1));
        }
        Vec2 diff = centerLocal - closest;
        double dist = length(diff);
        if (dist > circleBody.shape.radius()) {
            return false;
        }
        normalLocal = dist > 1e-9 ? diff * (1.0 / dist) : normals[faceIndex];
        contactLocal = closest;
        penetration = circleBody.shape.radius() - dist;
    }

    normalWorldOut = rotate(normalLocal, polyBody.angle);
    contactWorldOut = polyBody.position + rotate(contactLocal, polyBody.angle);
    penetrationOut = penetration;
    return true;
}

}

inline bool collide(Body& a, Body& b, Manifold& m) {
    m.a = &a;
    m.b = &b;
    m.contacts.clear();

    if (a.shape.type() == ShapeType::Circle && b.shape.type() == ShapeType::Circle) {
        return collision_detail::circleVsCircle(a, b, m);
    }
    if (a.shape.type() == ShapeType::Circle && b.shape.type() == ShapeType::Polygon) {
        Vec2 normal;
        Vec2 point;
        double penetration;
        if (!collision_detail::circleVsPolygonRaw(a, b, normal, point, penetration)) {
            return false;
        }
        m.normal = Vec2(-normal.x, -normal.y);
        m.contacts.push_back(Contact{point, penetration});
        return true;
    }
    if (a.shape.type() == ShapeType::Polygon && b.shape.type() == ShapeType::Circle) {
        Vec2 normal;
        Vec2 point;
        double penetration;
        if (!collision_detail::circleVsPolygonRaw(b, a, normal, point, penetration)) {
            return false;
        }
        m.normal = normal;
        m.contacts.push_back(Contact{point, penetration});
        return true;
    }
    return collision_detail::polygonVsPolygon(a, b, m);
}
