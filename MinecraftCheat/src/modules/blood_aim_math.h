#pragma once
#include <cmath>
#include <limits>

namespace BloodAim {
struct Point { double x, y, z; };
struct Angles { float yaw, pitch; };

inline double Wrap(double angle) {
    angle = std::fmod(angle, 360.0);
    if (angle >= 180.0) angle -= 360.0;
    if (angle < -180.0) angle += 360.0;
    return angle;
}

inline Angles Face(Point target, Point player, float eye, float yaw, float pitch, bool body = false) {
    double x = target.x - player.x;
    // 1.62 — точный уровень глаз игрока Minecraft (между глаз), 1.2 — уровень груди (bodyAim)
    double y = target.y + (body ? 1.2 : 1.62) - (player.y + double(eye));
    double z = target.z - player.z;
    double horizontal = std::sqrt(x * x + z * z);
    double ty = std::atan2(z, x) * 180.0 / 3.141592653589793 - 90.0;
    double tp = -std::atan2(y, horizontal) * 180.0 / 3.141592653589793;
    return { float(double(yaw) + Wrap(ty - double(yaw))), float(double(pitch) + Wrap(tp - double(pitch))) };
}

inline float MouseDistance(Point target, Point player, float eye, float yaw, float pitch) {
    auto needed = Face(target, player, eye, yaw, pitch);
    float dy = static_cast<float>(Wrap(yaw - needed.yaw));
    float dp = static_cast<float>(Wrap(pitch - needed.pitch));
    float distance = std::sqrt(dy * dy + dp * dp);
    if (std::isnan(distance)) return 9999.0f;
    return distance;
}

inline Point Predict(Point target, Point lastRender, Point previousTick, Point player, double partial, float predictSetting, float range, float vertical) {
    float dx = float(player.x - target.x);
    float dz = float(player.z - target.z);
    float distance = std::sqrt(dx * dx + dz * dz);
    float predict = predictSetting + (range > 0.001f ? (distance / range) : 0.0f);
    double x = lastRender.x + (target.x - lastRender.x) * partial + (target.x - previousTick.x) * double(predict);
    double z = lastRender.z + (target.z - lastRender.z) * partial + (target.z - previousTick.z) * double(predict);
    double y = lastRender.y + (target.y - lastRender.y) * partial;
    // При vertical == 0 цель строго между глаз. Чем выше vertical (до 2.0), тем выше берём упреждение на дистанции
    if (vertical > 0.0f) {
        y += double(vertical) * (double(distance * distance) * 0.00035 + double(distance) * 0.005);
    }
    return { double(float(x)), double(float(y)), double(float(z)) };
}
}

