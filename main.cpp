/*#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <vector>
#include <unordered_map>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <algorithm>
#include <cstdint>

#define MAX_SPHERES 20000

// ============================================================
// FENETRES / CAMERA
// ============================================================
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

bool cameraActive = false;
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
float yaw = -90.0f;
float pitch = 0.0f;
bool firstMouse = true;

glm::vec3 cameraPos(0.0f, 5.0f, 15.0f);
glm::vec3 cameraFront(0.0f, -0.3f, -1.0f);
glm::vec3 cameraUp(0.0f, 1.0f, 0.0f);

float deltaTime = 0.0f;
float lastFrame = 0.0f;

// ============================================================
// MODE EDITION / OBSTACLE SPHERIQUE
// ============================================================
bool editMode = false;
bool hasSphere = false;
bool mWasPressed = false;

glm::vec3 spherePos(0.0f);
float sphereRadius = 3.5f;
float sphereDepth = 5.0f;

// ============================================================
// BOITE
// ============================================================
float boxSizeX = 10.0f;
float boxSizeY = 10.0f;
float boxSizeZ = 10.0f;
float collisionDamping = 0.45f;

// ============================================================
// PARTICULES / SPAWN
// ============================================================
float particleRadius = 0.18f;
float renderRadius = 0.20f;
float spawnRate = 120.0f; // particules / seconde
float spawnAccumulator = 0.0f;
bool autoSpawn = true;

// ============================================================
// PARAMETRES SPH
// ============================================================
static constexpr float PI = 3.14159265358979323846f;

float smoothingRadius = 0.85f;
float particleMass = 1.0f;
float targetDensity = 7.0f;
float pressureMultiplier = 18.0f;
float nearPressureMultiplier = 10.0f;
float viscosityStrength = 0.10f;
float gravityStrength = 9.81f;
int simulationSubsteps = 2;
float maxSimulationDt = 1.0f / 60.0f;

// ============================================================
// GEOMETRIE BOITE
// ============================================================
float boxVertices[] = {
    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f
};

unsigned int boxIndices[] = {
    0, 1, 1, 2, 2, 3, 3, 0,
    4, 5, 5, 6, 6, 7, 7, 4,
    0, 4, 1, 5, 2, 6, 3, 7
};

// ============================================================
// STRUCTURE PARTICULE
// ============================================================
struct Sphere {
    glm::vec3 pos{0.0f};
    glm::vec3 predictedPos{0.0f};
    glm::vec3 vel{0.0f};
    glm::vec3 color{0.15f, 0.45f, 1.0f};

    float density = 0.0f;
    float nearDensity = 0.0f;
};

// ============================================================
// HASH GRID 3D
// ============================================================
struct Grid {
    float cellSize;
    std::unordered_map<std::int64_t, std::vector<int>> cells;

    explicit Grid(float cs) : cellSize(cs) {}

    static std::int64_t hashCell(int x, int y, int z) {
        // Mélange simple 64-bit. Les coordonnées négatives sont acceptées.
        std::int64_t hx = static_cast<std::int64_t>(x) * 73856093LL;
        std::int64_t hy = static_cast<std::int64_t>(y) * 19349663LL;
        std::int64_t hz = static_cast<std::int64_t>(z) * 83492791LL;
        return hx ^ hy ^ hz;
    }

    glm::ivec3 getCellCoord(const glm::vec3& p) const {
        return glm::ivec3(
            static_cast<int>(std::floor(p.x / cellSize)),
            static_cast<int>(std::floor(p.y / cellSize)),
            static_cast<int>(std::floor(p.z / cellSize))
        );
    }

    void setCellSize(float cs) {
        cellSize = std::max(cs, 0.0001f);
    }

    void clear() {
        cells.clear();
    }

    void addSphere(int idx, const glm::vec3& pos) {
        glm::ivec3 c = getCellCoord(pos);
        cells[hashCell(c.x, c.y, c.z)].push_back(idx);
    }

    template <typename F>
    void forEachNearby(const glm::vec3& pos, F&& callback) const {
        glm::ivec3 c = getCellCoord(pos);

        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    auto it = cells.find(hashCell(c.x + dx, c.y + dy, c.z + dz));
                    if (it == cells.end()) continue;

                    for (int index : it->second)
                        callback(index);
                }
            }
        }
    }
};

// ============================================================
// SHADERS DE RENDU
// ============================================================
const char* vertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* fragmentShaderSrc = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 color;
void main() {
    FragColor = vec4(color, 1.0);
}
)";

// ============================================================
// PROTOTYPES
// ============================================================
void processInput(GLFWwindow* window);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);

glm::vec3 getRayFromMouse(double mouseX, double mouseY,
                          const glm::mat4& projection,
                          const glm::mat4& view);

void generateSphere(std::vector<float>& vertices,
                    std::vector<unsigned int>& indices,
                    unsigned int sectorCount,
                    unsigned int stackCount);

unsigned int createShaderProgram();

// ============================================================
// SPH KERNELS 3D
// ============================================================
float SmoothingKernelPoly6(float dst, float radius) {
    if (dst >= radius) return 0.0f;

    float r2 = radius * radius;
    float x = r2 - dst * dst;
    float scale = 315.0f / (64.0f * PI * std::pow(radius, 9.0f));
    return x * x * x * scale;
}

float SpikyKernelPow2(float dst, float radius) {
    if (dst >= radius) return 0.0f;

    float v = radius - dst;
    float scale = 15.0f / (2.0f * PI * std::pow(radius, 5.0f));
    return v * v * scale;
}

float SpikyKernelPow3(float dst, float radius) {
    if (dst >= radius) return 0.0f;

    float v = radius - dst;
    float scale = 15.0f / (PI * std::pow(radius, 6.0f));
    return v * v * v * scale;
}

float DerivativeSpikyPow2(float dst, float radius) {
    if (dst > radius) return 0.0f;

    float v = radius - dst;
    float scale = 15.0f / (PI * std::pow(radius, 5.0f));
    return -v * scale;
}

float DerivativeSpikyPow3(float dst, float radius) {
    if (dst > radius) return 0.0f;

    float v = radius - dst;
    float scale = 45.0f / (PI * std::pow(radius, 6.0f));
    return -v * v * scale;
}

float PressureFromDensity(float density) {
    return (density - targetDensity) * pressureMultiplier;
}

float NearPressureFromDensity(float nearDensity) {
    return nearDensity * nearPressureMultiplier;
}

// ============================================================
// SIMULATION SPH
// ============================================================
void rebuildGrid(std::vector<Sphere>& spheres, Grid& grid) {
    grid.setCellSize(smoothingRadius);
    grid.clear();

    for (size_t i = 0; i < spheres.size(); ++i)
        grid.addSphere(static_cast<int>(i), spheres[i].predictedPos);
}

void calculateDensities(std::vector<Sphere>& spheres, const Grid& grid) {
    const float sqrRadius = smoothingRadius * smoothingRadius;

    for (size_t i = 0; i < spheres.size(); ++i) {
        float density = 0.0f;
        float nearDensity = 0.0f;
        const glm::vec3 pos = spheres[i].predictedPos;

        grid.forEachNearby(pos, [&](int j) {
            glm::vec3 offset = spheres[j].predictedPos - pos;
            float sqrDst = glm::dot(offset, offset);
            if (sqrDst > sqrRadius) return;

            float dst = std::sqrt(sqrDst);
            density += particleMass * SpikyKernelPow2(dst, smoothingRadius);
            nearDensity += particleMass * SpikyKernelPow3(dst, smoothingRadius);
        });

        spheres[i].density = std::max(density, 0.0001f);
        spheres[i].nearDensity = std::max(nearDensity, 0.0001f);
    }
}

void calculatePressureForces(std::vector<Sphere>& spheres, const Grid& grid, float dt) {
    const float sqrRadius = smoothingRadius * smoothingRadius;
    std::vector<glm::vec3> accelerations(spheres.size(), glm::vec3(0.0f));

    for (size_t i = 0; i < spheres.size(); ++i) {
        const glm::vec3 pos = spheres[i].predictedPos;
        const float density = spheres[i].density;
        const float nearDensity = spheres[i].nearDensity;
        const float pressure = PressureFromDensity(density);
        const float nearPressure = NearPressureFromDensity(nearDensity);

        glm::vec3 pressureForce(0.0f);

        grid.forEachNearby(pos, [&](int j) {
            if (j == static_cast<int>(i)) return;

            glm::vec3 offset = spheres[j].predictedPos - pos;
            float sqrDst = glm::dot(offset, offset);
            if (sqrDst > sqrRadius) return;

            float dst = std::sqrt(sqrDst);
            glm::vec3 dir;

            if (dst > 0.00001f)
                dir = offset / dst;
            else
                dir = glm::vec3(0.0f, 1.0f, 0.0f);

            float neighbourDensity = std::max(spheres[j].density, 0.0001f);
            float neighbourNearDensity = std::max(spheres[j].nearDensity, 0.0001f);

            float neighbourPressure = PressureFromDensity(neighbourDensity);
            float neighbourNearPressure = NearPressureFromDensity(neighbourNearDensity);

            float sharedPressure = 0.5f * (pressure + neighbourPressure);
            float sharedNearPressure = 0.5f * (nearPressure + neighbourNearPressure);

            pressureForce += dir
                * DerivativeSpikyPow2(dst, smoothingRadius)
                * sharedPressure
                * particleMass / neighbourDensity;

            pressureForce += dir
                * DerivativeSpikyPow3(dst, smoothingRadius)
                * sharedNearPressure
                * particleMass / neighbourNearDensity;
        });

        accelerations[i] = pressureForce / density;
    }

    for (size_t i = 0; i < spheres.size(); ++i)
        spheres[i].vel += accelerations[i] * dt;
}

void calculateViscosity(std::vector<Sphere>& spheres, const Grid& grid, float dt) {
    const float sqrRadius = smoothingRadius * smoothingRadius;
    std::vector<glm::vec3> deltaVelocity(spheres.size(), glm::vec3(0.0f));

    for (size_t i = 0; i < spheres.size(); ++i) {
        const glm::vec3 pos = spheres[i].predictedPos;
        const glm::vec3 velocity = spheres[i].vel;
        glm::vec3 viscosityForce(0.0f);

        grid.forEachNearby(pos, [&](int j) {
            if (j == static_cast<int>(i)) return;

            glm::vec3 offset = spheres[j].predictedPos - pos;
            float sqrDst = glm::dot(offset, offset);
            if (sqrDst > sqrRadius) return;

            float dst = std::sqrt(sqrDst);
            float influence = SmoothingKernelPoly6(dst, smoothingRadius);
            viscosityForce += (spheres[j].vel - velocity) * influence;
        });

        deltaVelocity[i] = viscosityForce * viscosityStrength * dt;
    }

    for (size_t i = 0; i < spheres.size(); ++i)
        spheres[i].vel += deltaVelocity[i];
}

void resolveBoxCollision(Sphere& s) {
    const float r = particleRadius;

    const float minX = -boxSizeX + r;
    const float maxX =  boxSizeX - r;
    const float minY = -boxSizeY + r;
    const float maxY =  boxSizeY - r;
    const float minZ = -boxSizeZ + r;
    const float maxZ =  boxSizeZ - r;

    if (s.pos.x < minX) {
        s.pos.x = minX;
        if (s.vel.x < 0.0f) s.vel.x *= -collisionDamping;
    } else if (s.pos.x > maxX) {
        s.pos.x = maxX;
        if (s.vel.x > 0.0f) s.vel.x *= -collisionDamping;
    }

    if (s.pos.y < minY) {
        s.pos.y = minY;
        if (s.vel.y < 0.0f) s.vel.y *= -collisionDamping;
    } else if (s.pos.y > maxY) {
        s.pos.y = maxY;
        if (s.vel.y > 0.0f) s.vel.y *= -collisionDamping;
    }

    if (s.pos.z < minZ) {
        s.pos.z = minZ;
        if (s.vel.z < 0.0f) s.vel.z *= -collisionDamping;
    } else if (s.pos.z > maxZ) {
        s.pos.z = maxZ;
        if (s.vel.z > 0.0f) s.vel.z *= -collisionDamping;
    }
}

void resolveObstacleCollision(Sphere& s) {
    if (!hasSphere) return;

    glm::vec3 offset = s.pos - spherePos;
    float dst2 = glm::dot(offset, offset);
    float minDst = sphereRadius + particleRadius;

    if (dst2 >= minDst * minDst) return;

    float dst = std::sqrt(std::max(dst2, 0.0000001f));
    glm::vec3 normal = (dst > 0.0001f) ? offset / dst : glm::vec3(0.0f, 1.0f, 0.0f);

    s.pos = spherePos + normal * minDst;

    float vn = glm::dot(s.vel, normal);
    if (vn < 0.0f)
        s.vel -= (1.0f + collisionDamping) * vn * normal;
}

void simulateFluid(std::vector<Sphere>& spheres, Grid& grid, float frameDt) {
    if (spheres.empty()) return;

    frameDt = std::clamp(frameDt, 0.0f, maxSimulationDt);
    int substeps = std::max(1, simulationSubsteps);
    float dt = frameDt / static_cast<float>(substeps);

    for (int step = 0; step < substeps; ++step) {
        // 1) Forces externes + position prédite
        for (Sphere& s : spheres) {
            s.vel += glm::vec3(0.0f, -gravityStrength, 0.0f) * dt;
            s.predictedPos = s.pos + s.vel * dt;
        }

        // 2) Spatial hash sur les positions prédites
        rebuildGrid(spheres, grid);

        // 3) Densité / near density
        calculateDensities(spheres, grid);

        // 4) Pression
        calculatePressureForces(spheres, grid, dt);

        // 5) Viscosité
        calculateViscosity(spheres, grid, dt);

        // 6) Intégration finale + collisions
        for (Sphere& s : spheres) {
            s.pos += s.vel * dt;
            resolveBoxCollision(s);
            resolveObstacleCollision(s);
        }
    }
}

// ============================================================
// CREATION / SPAWN
// ============================================================
Sphere makeParticle(const glm::vec3& pos) {
    Sphere s;
    s.pos = pos;
    s.predictedPos = pos;
    s.vel = glm::vec3(0.0f);

    // Légère variation bleue pour garder ton rendu particulaire lisible.
    float variation = static_cast<float>(std::rand() % 20) / 100.0f;
    s.color = glm::vec3(0.10f + variation * 0.25f,
                        0.38f + variation * 0.20f,
                        0.90f + variation * 0.10f);
    return s;
}

void spawnParticles(std::vector<Sphere>& spheres, float dt) {
    if (!autoSpawn || spawnRate <= 0.0f || spheres.size() >= MAX_SPHERES)
        return;

    spawnAccumulator += spawnRate * dt;
    int count = static_cast<int>(spawnAccumulator);
    spawnAccumulator -= static_cast<float>(count);

    count = std::min(count, 300); // protection lors d'un gros freeze

    for (int n = 0; n < count && spheres.size() < MAX_SPHERES; ++n) {
        float rx = (static_cast<float>(std::rand() % 1000) / 999.0f - 0.5f) * 2.5f;
        float rz = (static_cast<float>(std::rand() % 1000) / 999.0f - 0.5f) * 2.5f;
        float ry = (static_cast<float>(std::rand() % 1000) / 999.0f) * 0.5f;

        glm::vec3 p(rx, boxSizeY - 1.0f - ry, rz);
        spheres.push_back(makeParticle(p));
    }
}

// ============================================================
// RENDU / EDITION
// ============================================================
void drawSphere(unsigned int shaderProgram,
                unsigned int VAO,
                size_t indexCount,
                const glm::vec3& position,
                float radius,
                const glm::vec3& color,
                const glm::mat4& view,
                const glm::mat4& projection) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
    model = glm::scale(model, glm::vec3(radius));

    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(shaderProgram, "color"), 1, glm::value_ptr(color));

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr);
}

void updateAndDrawEditObstacle(GLFWwindow* window,
                               unsigned int shaderProgram,
                               unsigned int sphereVAO,
                               size_t indexCount,
                               const glm::mat4& view,
                               const glm::mat4& projection) {
    if (!editMode) return;

    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    glm::vec3 ray = getRayFromMouse(mouseX, mouseY, projection, view);

    if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)
        sphereDepth += 3.0f * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS)
        sphereDepth -= 3.0f * deltaTime;

    sphereDepth = std::max(0.5f, sphereDepth);
    glm::vec3 previewPos = cameraPos + glm::normalize(ray) * sphereDepth;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        spherePos = previewPos;
        hasSphere = true;
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    drawSphere(shaderProgram, sphereVAO, indexCount,
               previewPos, sphereRadius,
               hasSphere ? glm::vec3(0.0f, 1.0f, 1.0f) : glm::vec3(1.0f, 1.0f, 0.0f),
               view, projection);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Affiche aussi l'obstacle réellement placé s'il n'est plus exactement sous le curseur.
    if (hasSphere && glm::length(previewPos - spherePos) > 0.01f) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        drawSphere(shaderProgram, sphereVAO, indexCount,
                   spherePos, sphereRadius,
                   glm::vec3(0.0f, 0.8f, 1.0f),
                   view, projection);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

// ============================================================
// MAIN
// ============================================================
int main() {
    // --- Init GLFW ---
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return -1;
    }

    // Ton ancien réglage qui fonctionnait
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // NE PAS demander GLFW_OPENGL_CORE_PROFILE
    // Ton ancien programme fonctionnait sans cette ligne.

    GLFWwindow* window = glfwCreateWindow(
        SCR_WIDTH,
        SCR_HEIGHT,
        "Simulateur SPH 3D",
        nullptr,
        nullptr
    );

    if (!window) {
        const char* description = nullptr;
        int code = glfwGetError(&description);

        std::cerr << "GLFW error " << code << ": "
                  << (description ? description : "unknown")
                  << std::endl;

        glfwTerminate();
        return -1;
    }

    // --- Contexte OpenGL ---
    glfwMakeContextCurrent(window);

    // --- GLAD ---
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";

        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // --- Afficher la version réellement obtenue ---
    std::cout << "OpenGL version: "
              << glGetString(GL_VERSION)
              << std::endl;

    std::cout << "GLSL version: "
              << glGetString(GL_SHADING_LANGUAGE_VERSION)
              << std::endl;

    // --- OpenGL ---
    glEnable(GL_DEPTH_TEST);

    // --- Souris ---
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);

    // La fenêtre ImGui partage le contexte OpenGL avec la principale.
    GLFWwindow* window2 = glfwCreateWindow(460, 650, "Parametres du fluide", nullptr, window);
    if (!window2) {
        std::cerr << "Failed to create ImGui window\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window2);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window2, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    glfwMakeContextCurrent(window);
    unsigned int shaderProgram = createShaderProgram();

    // Sphère de rendu
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    unsigned int sectorCount = 16;
    unsigned int stackCount = 12;
    generateSphere(vertices, indices, sectorCount, stackCount);

    unsigned int sphereVAO, sphereVBO, sphereEBO;
    glGenVertexArrays(1, &sphereVAO);
    glGenBuffers(1, &sphereVBO);
    glGenBuffers(1, &sphereEBO);

    glBindVertexArray(sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Boîte
    unsigned int boxVAO, boxVBO, boxEBO;
    glGenVertexArrays(1, &boxVAO);
    glGenBuffers(1, &boxVBO);
    glGenBuffers(1, &boxEBO);

    glBindVertexArray(boxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, boxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(boxVertices), boxVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, boxEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(boxIndices), boxIndices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    std::vector<Sphere> spheres;
    spheres.reserve(MAX_SPHERES);
    Grid grid(smoothingRadius);

    lastFrame = static_cast<float>(glfwGetTime());

    while (!glfwWindowShouldClose(window) && !glfwWindowShouldClose(window2)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        deltaTime = std::clamp(deltaTime, 0.0f, 0.05f);

        glfwPollEvents();
        processInput(window);

        // ---------------- MAIN WINDOW ----------------
        glfwMakeContextCurrent(window);
        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.035f, 0.045f, 0.075f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            static_cast<float>(SCR_WIDTH) / static_cast<float>(SCR_HEIGHT),
            0.1f,
            200.0f
        );

        spawnParticles(spheres, deltaTime);
        simulateFluid(spheres, grid, deltaTime);

        glUseProgram(shaderProgram);

        // Boîte fil de fer
        glm::mat4 boxModel = glm::scale(glm::mat4(1.0f), glm::vec3(boxSizeX, boxSizeY, boxSizeZ));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(boxModel));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(glGetUniformLocation(shaderProgram, "color"), 1.0f, 1.0f, 1.0f);
        glBindVertexArray(boxVAO);
        glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, nullptr);

        // Particules
        for (const Sphere& s : spheres) {
            drawSphere(shaderProgram, sphereVAO, indices.size(),
                       s.pos, renderRadius, s.color,
                       view, projection);
        }

        // Obstacle / mode édition
        updateAndDrawEditObstacle(window, shaderProgram, sphereVAO, indices.size(), view, projection);

        glfwSwapBuffers(window);

        // ---------------- IMGUI WINDOW ----------------
        glfwMakeContextCurrent(window2);
        int uiWidth, uiHeight;
        glfwGetFramebufferSize(window2, &uiWidth, &uiHeight);
        glViewport(0, 0, uiWidth, uiHeight);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Simulation");
        ImGui::Text("Particules : %d / %d", static_cast<int>(spheres.size()), MAX_SPHERES);
        ImGui::Text("FPS : %.1f", ImGui::GetIO().Framerate);
        ImGui::Checkbox("Spawn automatique", &autoSpawn);
        ImGui::SliderFloat("Spawn / seconde", &spawnRate, 0.0f, 1000.0f, "%.0f");

        if (ImGui::Button("Ajouter 500 particules")) {
            for (int n = 0; n < 500 && spheres.size() < MAX_SPHERES; ++n) {
                float rx = (static_cast<float>(std::rand() % 1000) / 999.0f - 0.5f) * 3.0f;
                float rz = (static_cast<float>(std::rand() % 1000) / 999.0f - 0.5f) * 3.0f;
                float ry = (static_cast<float>(std::rand() % 1000) / 999.0f) * 2.0f;
                spheres.push_back(makeParticle(glm::vec3(rx, boxSizeY - 1.0f - ry, rz)));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            spheres.clear();
            spawnAccumulator = 0.0f;
        }
        ImGui::End();

        ImGui::Begin("SPH - Physique");
        ImGui::SliderFloat("Smoothing radius", &smoothingRadius, 0.25f, 2.5f, "%.3f");
        ImGui::SliderFloat("Target density", &targetDensity, 0.5f, 30.0f, "%.3f");
        ImGui::SliderFloat("Pressure", &pressureMultiplier, 0.0f, 100.0f, "%.2f");
        ImGui::SliderFloat("Near pressure", &nearPressureMultiplier, 0.0f, 100.0f, "%.2f");
        ImGui::SliderFloat("Viscosite", &viscosityStrength, 0.0f, 5.0f, "%.3f");
        ImGui::SliderFloat("Masse particule", &particleMass, 0.05f, 5.0f, "%.3f");
        ImGui::SliderFloat("Gravite", &gravityStrength, 0.0f, 30.0f, "%.2f");
        ImGui::SliderInt("Sous-etapes", &simulationSubsteps, 1, 8);
        ImGui::End();

        ImGui::Begin("Boite / collisions");
        ImGui::SliderFloat("Boite X", &boxSizeX, 2.0f, 30.0f);
        ImGui::SliderFloat("Boite Y", &boxSizeY, 2.0f, 30.0f);
        ImGui::SliderFloat("Boite Z", &boxSizeZ, 2.0f, 30.0f);
        ImGui::SliderFloat("Rebond", &collisionDamping, 0.0f, 1.0f, "%.2f");
        ImGui::End();

        ImGui::Begin("Rendu / particules");
        ImGui::SliderFloat("Rayon physique", &particleRadius, 0.03f, 0.5f, "%.3f");
        ImGui::SliderFloat("Rayon visible", &renderRadius, 0.03f, 0.5f, "%.3f");
        ImGui::TextWrapped("Le rayon visible ne change que le dessin. Le rayon physique sert aux collisions avec la boite et l'obstacle.");
        ImGui::End();

        ImGui::Begin("Mode edition / obstacle");
        ImGui::Checkbox("Mode edition (M)", &editMode);
        ImGui::SliderFloat("Rayon obstacle", &sphereRadius, 0.2f, 8.0f);
        ImGui::SliderFloat("Profondeur obstacle", &sphereDepth, 0.5f, 30.0f);
        ImGui::Text("Z / B : eloigner / rapprocher");
        ImGui::Text("Clic gauche : placer l'obstacle");
        if (ImGui::Button("Supprimer obstacle"))
            hasSphere = false;
        ImGui::Text("Obstacle place : %s", hasSphere ? "oui" : "non");
        ImGui::End();

        ImGui::Begin("Camera");
        ImGui::Text("Clic droit : activer/desactiver camera");
        ImGui::Text("WASD : mouvement");
        ImGui::Text("Q/E : descendre/monter");
        ImGui::Text("M : mode edition");
        ImGui::Text("ESC : quitter");
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window2);
    }

    glfwMakeContextCurrent(window2);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwMakeContextCurrent(window);
    glDeleteProgram(shaderProgram);
    glDeleteVertexArrays(1, &sphereVAO);
    glDeleteBuffers(1, &sphereVBO);
    glDeleteBuffers(1, &sphereEBO);
    glDeleteVertexArrays(1, &boxVAO);
    glDeleteBuffers(1, &boxVBO);
    glDeleteBuffers(1, &boxEBO);

    glfwDestroyWindow(window2);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// ============================================================
// INPUT / CAMERA
// ============================================================
void processInput(GLFWwindow* window) {
    float cameraSpeed = 5.0f * deltaTime;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        cameraPos += cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        cameraPos -= cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        cameraPos.y += cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        cameraPos.y -= cameraSpeed;

    int mState = glfwGetKey(window, GLFW_KEY_M);
    if (mState == GLFW_PRESS && !mWasPressed) {
        editMode = !editMode;
        cameraActive = false;
        firstMouse = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    mWasPressed = (mState == GLFW_PRESS);

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (!cameraActive || editMode) return;

    if (firstMouse) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
        return;
    }

    float xoffset = static_cast<float>(xpos) - lastX;
    float yoffset = lastY - static_cast<float>(ypos);
    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);

    const float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;
    pitch = std::clamp(pitch, -89.0f, 89.0f);

    glm::vec3 front;
    front.x = std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
    front.y = std::sin(glm::radians(pitch));
    front.z = std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int /*mods* /) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS && !editMode) {
        cameraActive = !cameraActive;
        firstMouse = true;

        glfwSetInputMode(
            window,
            GLFW_CURSOR,
            cameraActive ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
        );
    }
}

// ============================================================
// UTILITAIRES RENDU
// ============================================================
glm::vec3 getRayFromMouse(double mouseX, double mouseY,
                          const glm::mat4& projection,
                          const glm::mat4& view) {
    float x = (2.0f * static_cast<float>(mouseX)) / static_cast<float>(SCR_WIDTH) - 1.0f;
    float y = 1.0f - (2.0f * static_cast<float>(mouseY)) / static_cast<float>(SCR_HEIGHT);

    glm::vec4 rayClip(x, y, -1.0f, 1.0f);
    glm::vec4 rayEye = glm::inverse(projection) * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

    glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
    return glm::normalize(rayWorld);
}

void generateSphere(std::vector<float>& vertices,
                    std::vector<unsigned int>& indices,
                    unsigned int sectorCount,
                    unsigned int stackCount) {
    vertices.clear();
    indices.clear();

    for (unsigned int i = 0; i <= stackCount; ++i) {
        float stackAngle = PI / 2.0f - static_cast<float>(i) * (PI / static_cast<float>(stackCount));
        float xy = std::cos(stackAngle);
        float z = std::sin(stackAngle);

        for (unsigned int j = 0; j <= sectorCount; ++j) {
            float sectorAngle = static_cast<float>(j) * (2.0f * PI / static_cast<float>(sectorCount));
            vertices.push_back(xy * std::cos(sectorAngle));
            vertices.push_back(xy * std::sin(sectorAngle));
            vertices.push_back(z);
        }
    }

    for (unsigned int i = 0; i < stackCount; ++i) {
        unsigned int k1 = i * (sectorCount + 1);
        unsigned int k2 = k1 + sectorCount + 1;

        for (unsigned int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            if (i != 0) {
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);
            }

            if (i != stackCount - 1) {
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
    }
}

unsigned int createShaderProgram() {
    auto compile = [](GLenum type, const char* source) -> unsigned int {
        unsigned int shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        int success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[2048];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "Shader compilation error:\n" << log << '\n';
        }
        return shader;
    };

    unsigned int vs = compile(GL_VERTEX_SHADER, vertexShaderSrc);
    unsigned int fs = compile(GL_FRAGMENT_SHADER, fragmentShaderSrc);

    unsigned int program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Shader link error:\n" << log << '\n';
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}*/
#include <cstdint>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <vector>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <string>

#define MAX_SPHERES 200000

const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

bool cameraActive = false;
float lastX = SCR_WIDTH / 2.0f, lastY = SCR_HEIGHT / 2.0f;
float yaw = -90.0f, pitch = 0.0f;
bool firstMouse = true;

glm::vec3 cameraPos(0.0f, 5.0f, 15.0f);
glm::vec3 cameraFront(0.0f, -0.3f, -1.0f);
glm::vec3 cameraUp(0.0f, 1.0f, 0.0f);

float deltaTime = 0.0f, lastFrame = 0.0f;

bool editMode = false;
bool hasSphere = false;
bool mWasPressed = false;

glm::vec3 spherePos(0.0f);
float sphereRadius = 3.5f;
float sphereDepth = 5.0f;

float boxSizeX = 10.0f, boxSizeY = 10.0f, boxSizeZ = 10.0f;
float collisionDamping = 0.45f;

float particleRadius = 0.18f;
float renderRadius = 0.20f;
float spawnRate = 120.0f;
float spawnAccumulator = 0.0f;
bool autoSpawn = true;

static constexpr float PI = 3.14159265358979323846f;

float smoothingRadius = 0.85f;
float particleMass = 1.0f;
float targetDensity = 7.0f;
float pressureMultiplier = 18.0f;
float nearPressureMultiplier = 10.0f;
float viscosityStrength = 0.10f;
float gravityStrength = 9.81f;
int simulationSubsteps = 2;
float maxSimulationDt = 1.0f / 60.0f;

float boxVertices[] = {
    -1,-1,-1,  1,-1,-1,  1,1,-1,  -1,1,-1,
    -1,-1, 1,  1,-1, 1,  1,1, 1,  -1,1, 1
};

unsigned int boxIndices[] = {
    0,1, 1,2, 2,3, 3,0,
    4,5, 5,6, 6,7, 7,4,
    0,4, 1,5, 2,6, 3,7
};

struct Sphere {
    glm::vec3 pos{0.0f};
    glm::vec3 predictedPos{0.0f};
    glm::vec3 vel{0.0f};
    glm::vec3 color{0.15f,0.45f,1.0f};
    float density = 0.0f;
    float nearDensity = 0.0f;
};

// ------------------------------------------------------------
// Liquid rendering
// ------------------------------------------------------------

GLuint liquidDepthFBO = 0;
GLuint liquidDepthTexture = 0;
GLuint liquidDepthShader = 0;

GLuint liquidThicknessFBO = 0;
GLuint liquidThicknessTexture = 0;
GLuint liquidThicknessShader = 0;

GLuint liquidBlurFBO = 0;
GLuint liquidBlurTexture = 0;
GLuint liquidBlurShader = 0;

GLuint liquidCompositeShader = 0;

GLuint fullscreenVAO = 0;
GLuint fullscreenVBO = 0;

int liquidWidth = SCR_WIDTH;
int liquidHeight = SCR_HEIGHT;

// 80 bytes, matching std430 layout in fluid.comp.
struct ParticleGPU {
    glm::vec4 pos;
    glm::vec4 predicted;
    glm::vec4 vel;
    glm::vec4 color;
    glm::vec4 densityNear;
};

void processInput(GLFWwindow* window);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
glm::vec3 getRayFromMouse(double mouseX, double mouseY,
                          const glm::mat4& projection, const glm::mat4& view);

void generateSphere(std::vector<float>& vertices,
                    std::vector<unsigned int>& indices,
                    unsigned int sectorCount, unsigned int stackCount);

unsigned int createRenderShader();
unsigned int createSimpleShader();
unsigned int createComputeShaderFromSource(const char* source);

void checkGLError(const char* where) {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR)
        std::cerr << "OpenGL error at " << where << ": 0x" << std::hex << e << std::dec << "\n";
}

ParticleGPU toGPU(const Sphere& s) {
    ParticleGPU p{};
    p.pos = glm::vec4(s.pos, 1.0f);
    p.predicted = glm::vec4(s.predictedPos, 1.0f);
    p.vel = glm::vec4(s.vel, 0.0f);
    p.color = glm::vec4(s.color, 1.0f);
    p.densityNear = glm::vec4(0.0f);
    return p;
}

void uploadNewParticles(GLuint particleSSBO,
                        const std::vector<Sphere>& spheres,
                        size_t oldCount) {
    if (oldCount >= spheres.size()) return;

    std::vector<ParticleGPU> temp;
    temp.reserve(spheres.size() - oldCount);

    for (size_t i = oldCount; i < spheres.size(); ++i)
        temp.push_back(toGPU(spheres[i]));

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleSSBO);
    glBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<GLintptr>(oldCount * sizeof(ParticleGPU)),
        static_cast<GLsizeiptr>(temp.size() * sizeof(ParticleGPU)),
        temp.data()
    );
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void clearGPUCount() {
    // No GPU readback is needed: the CPU vector size is the active count.
}

void dispatchCompute(GLuint program, GLuint particleCount,
                     GLuint cellCounts, GLuint cellParticles,
                     GLuint mode) {
    glUseProgram(program);
    glUniform1ui(glGetUniformLocation(program, "uParticleCount"), particleCount);
    glUniform1ui(glGetUniformLocation(program, "uMode"), mode);
    glDispatchCompute((particleCount + 255u) / 256u, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    (void)cellCounts;
    (void)cellParticles;
}

void setComputeUniforms(GLuint program, float dt) {
    auto loc = [&](const char* n) { return glGetUniformLocation(program, n); };

    glUniform1f(loc("uDt"), dt);
    glUniform1f(loc("uSmoothingRadius"), smoothingRadius);
    glUniform1f(loc("uParticleMass"), particleMass);
    glUniform1f(loc("uTargetDensity"), targetDensity);
    glUniform1f(loc("uPressureMultiplier"), pressureMultiplier);
    glUniform1f(loc("uNearPressureMultiplier"), nearPressureMultiplier);
    glUniform1f(loc("uViscosityStrength"), viscosityStrength);
    glUniform1f(loc("uGravityStrength"), gravityStrength);
    glUniform1f(loc("uParticleRadius"), particleRadius);
    glUniform1f(loc("uCollisionDamping"), collisionDamping);

    glUniform3f(loc("uBoxMin"), -boxSizeX, -boxSizeY, -boxSizeZ);
    glUniform3f(loc("uBoxMax"),  boxSizeX,  boxSizeY,  boxSizeZ);

    glUniform1i(loc("uHasObstacle"), hasSphere ? 1 : 0);
    glUniform3fv(loc("uObstaclePos"), 1, glm::value_ptr(spherePos));
    glUniform1f(loc("uObstacleRadius"), sphereRadius);

    // Cell size is never smaller than 1.0. This keeps the fixed 64^3 grid
    // sufficient for the current maximum box size of 30.
    float cellSize = std::max(smoothingRadius, 1.0f);
    glm::vec3 origin(-boxSizeX, -boxSizeY, -boxSizeZ);

    int dimX = std::clamp(static_cast<int>(std::ceil((2.0f * boxSizeX) / cellSize)), 1, 64);
    int dimY = std::clamp(static_cast<int>(std::ceil((2.0f * boxSizeY) / cellSize)), 1, 64);
    int dimZ = std::clamp(static_cast<int>(std::ceil((2.0f * boxSizeZ) / cellSize)), 1, 64);

    glUniform1f(loc("uCellSize"), cellSize);
    glUniform3fv(loc("uGridOrigin"), 1, glm::value_ptr(origin));
    glUniform3i(loc("uGridDim"), dimX, dimY, dimZ);
}

void simulateFluidGPU(GLuint computeProgram,
                      GLuint particleSSBO,
                      GLuint cellCountsSSBO,
                      GLuint cellParticlesSSBO,
                      GLuint count,
                      float frameDt) {
    if (count == 0) return;

    frameDt = std::clamp(frameDt, 0.0f, maxSimulationDt);
    int substeps = std::max(1, simulationSubsteps);
    float dt = frameDt / static_cast<float>(substeps);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cellCountsSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, cellParticlesSSBO);

    for (int step = 0; step < substeps; ++step) {
        // MODE 0: gravity + predicted positions
        glUseProgram(computeProgram);
        setComputeUniforms(computeProgram, dt);
        glUniform1ui(glGetUniformLocation(computeProgram, "uParticleCount"), count);
        glUniform1ui(glGetUniformLocation(computeProgram, "uMode"), 0);
        glDispatchCompute((count + 255u) / 256u, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // Reset cell counters.
        GLuint zero = 0;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, cellCountsSSBO);
        glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI,
                          GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // MODE 1: build GPU spatial grid
        glUseProgram(computeProgram);
        setComputeUniforms(computeProgram, dt);
        glUniform1ui(glGetUniformLocation(computeProgram, "uParticleCount"), count);
        glUniform1ui(glGetUniformLocation(computeProgram, "uMode"), 1);
        glDispatchCompute((count + 255u) / 256u, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // MODE 2: density
        glUseProgram(computeProgram);
        setComputeUniforms(computeProgram, dt);
        glUniform1ui(glGetUniformLocation(computeProgram, "uParticleCount"), count);
        glUniform1ui(glGetUniformLocation(computeProgram, "uMode"), 2);
        glDispatchCompute((count + 255u) / 256u, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // MODE 3: pressure + viscosity
        glUseProgram(computeProgram);
        setComputeUniforms(computeProgram, dt);
        glUniform1ui(glGetUniformLocation(computeProgram, "uParticleCount"), count);
        glUniform1ui(glGetUniformLocation(computeProgram, "uMode"), 3);
        glDispatchCompute((count + 255u) / 256u, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // MODE 4: final integration + collisions
        glUseProgram(computeProgram);
        setComputeUniforms(computeProgram, dt);
        glUniform1ui(glGetUniformLocation(computeProgram, "uParticleCount"), count);
        glUniform1ui(glGetUniformLocation(computeProgram, "uMode"), 4);
        glDispatchCompute((count + 255u) / 256u, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
}

Sphere makeParticle(const glm::vec3& pos) {
    Sphere s;
    s.pos = pos;
    s.predictedPos = pos;
    s.vel = glm::vec3(0.0f);

    float variation = static_cast<float>(std::rand() % 20) / 100.0f;
    s.color = glm::vec3(
        0.10f + variation * 0.25f,
        0.38f + variation * 0.20f,
        0.90f + variation * 0.10f
    );
    return s;
}

void spawnParticles(std::vector<Sphere>& spheres, float dt,
                    GLuint particleSSBO) {
    if (!autoSpawn || spawnRate <= 0.0f || spheres.size() >= MAX_SPHERES)
        return;

    size_t oldCount = spheres.size();

    spawnAccumulator += spawnRate * dt;
    int count = static_cast<int>(spawnAccumulator);
    spawnAccumulator -= static_cast<float>(count);
    count = std::min(count, 300);

    for (int n = 0; n < count && spheres.size() < MAX_SPHERES; ++n) {
        float rx = (static_cast<float>(std::rand() % 1000) / 999.0f - 0.5f) * 2.5f;
        float rz = (static_cast<float>(std::rand() % 1000) / 999.0f - 0.5f) * 2.5f;
        float ry = (static_cast<float>(std::rand() % 1000) / 999.0f) * 0.5f;
        spheres.push_back(makeParticle(glm::vec3(rx, boxSizeY - 1.0f - ry, rz)));
    }

    uploadNewParticles(particleSSBO, spheres, oldCount);
}

void add500(std::vector<Sphere>& spheres, GLuint particleSSBO) {
    size_t oldCount = spheres.size();

    for (int n = 0; n < 500 && spheres.size() < MAX_SPHERES; ++n) {
        float rx = (static_cast<float>(std::rand() % 1000) / 999.0f - 0.5f) * 3.0f;
        float rz = (static_cast<float>(std::rand() % 1000) / 999.0f - 0.5f) * 3.0f;
        float ry = (static_cast<float>(std::rand() % 1000) / 999.0f) * 2.0f;
        spheres.push_back(makeParticle(glm::vec3(rx, boxSizeY - 1.0f - ry, rz)));
    }

    uploadNewParticles(particleSSBO, spheres, oldCount);
}

void generateSphere(std::vector<float>& vertices,
                    std::vector<unsigned int>& indices,
                    unsigned int sectorCount,
                    unsigned int stackCount) {
    vertices.clear();
    indices.clear();

    for (unsigned int i = 0; i <= stackCount; ++i) {
        float stackAngle = PI / 2.0f - i * (PI / stackCount);
        float xy = std::cos(stackAngle);
        float z = std::sin(stackAngle);

        for (unsigned int j = 0; j <= sectorCount; ++j) {
            float sectorAngle = j * (2.0f * PI / sectorCount);
            vertices.push_back(xy * std::cos(sectorAngle));
            vertices.push_back(xy * std::sin(sectorAngle));
            vertices.push_back(z);
        }
    }

    for (unsigned int i = 0; i < stackCount; ++i) {
        unsigned int k1 = i * (sectorCount + 1);
        unsigned int k2 = k1 + sectorCount + 1;

        for (unsigned int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            if (i != 0) {
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);
            }
            if (i != stackCount - 1) {
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
    }
}

unsigned int compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Shader compilation error:\n" << log << "\n";
    }
    return shader;
}

unsigned int linkProgram(const std::vector<GLuint>& shaders) {
    GLuint program = glCreateProgram();
    for (GLuint s : shaders) glAttachShader(program, s);
    glLinkProgram(program);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Program link error:\n" << log << "\n";
    }

    for (GLuint s : shaders) {
        glDetachShader(program, s);
        glDeleteShader(s);
    }
    return program;
}

unsigned int createRenderShader() {
    const char* vs = R"(
#version 430 core
layout(location=0) in vec3 aPos;

struct Particle {
    vec4 pos;
    vec4 predicted;
    vec4 vel;
    vec4 color;
    vec4 densityNear;
};

layout(std430, binding=0) readonly buffer ParticleBuffer {
    Particle particles[];
};

uniform mat4 view;
uniform mat4 projection;
uniform float radius;

out vec3 vColor;

void main() {
    Particle p = particles[gl_InstanceID];
    vec3 worldPos = p.pos.xyz + aPos * radius;
    gl_Position = projection * view * vec4(worldPos, 1.0);
    vColor = p.color.rgb;
}
)";

    const char* fs = R"(
#version 430 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

    return linkProgram({
        compileShader(GL_VERTEX_SHADER, vs),
        compileShader(GL_FRAGMENT_SHADER, fs)
    });
}

unsigned int createSimpleShader() {
    const char* vs = R"(
#version 430 core
layout(location=0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    gl_Position = projection * view * model * vec4(aPos,1.0);
}
)";

    const char* fs = R"(
#version 430 core
out vec4 FragColor;
uniform vec3 color;
void main() {
    FragColor = vec4(color,1.0);
}
)";

    return linkProgram({
        compileShader(GL_VERTEX_SHADER, vs),
        compileShader(GL_FRAGMENT_SHADER, fs)
    });
}

unsigned int createComputeShaderFromSource(const char* source) {
    GLuint shader = compileShader(GL_COMPUTE_SHADER, source);
    if (!shader) return 0;
    return linkProgram({shader});
}

static const char* fluidComputeSource = R"GLSL(#version 430 core

#define GRID_SIZE 64
#define CELL_CAPACITY 128

layout(local_size_x = 256) in;

struct Particle {
    vec4 pos;
    vec4 predicted;
    vec4 vel;
    vec4 color;
    vec4 densityNear;
};

layout(std430, binding=0) buffer ParticleBuffer {
    Particle particles[];
};

layout(std430, binding=1) buffer CellCounts {
    uint cellCounts[];
};

layout(std430, binding=2) buffer CellParticles {
    uint cellParticles[];
};

uniform uint uParticleCount;
uniform uint uMode;

uniform float uDt;
uniform float uSmoothingRadius;
uniform float uParticleMass;
uniform float uTargetDensity;
uniform float uPressureMultiplier;
uniform float uNearPressureMultiplier;
uniform float uViscosityStrength;
uniform float uGravityStrength;
uniform float uParticleRadius;
uniform float uCollisionDamping;

uniform vec3 uBoxMin;
uniform vec3 uBoxMax;

uniform int uHasObstacle;
uniform vec3 uObstaclePos;
uniform float uObstacleRadius;

uniform float uCellSize;
uniform vec3 uGridOrigin;
uniform ivec3 uGridDim;

const float PI = 3.14159265358979323846;

float spiky2(float dst) {
    if (dst >= uSmoothingRadius) return 0.0;
    float v = uSmoothingRadius - dst;
    float scale = 15.0 / (2.0 * PI * pow(uSmoothingRadius,5.0));
    return v*v*scale;
}

float spiky3(float dst) {
    if (dst >= uSmoothingRadius) return 0.0;
    float v = uSmoothingRadius - dst;
    float scale = 15.0 / (PI * pow(uSmoothingRadius,6.0));
    return v*v*v*scale;
}

float dSpiky2(float dst) {
    if (dst > uSmoothingRadius) return 0.0;
    float v = uSmoothingRadius - dst;
    float scale = 15.0 / (PI * pow(uSmoothingRadius,5.0));
    return -v*scale;
}

float dSpiky3(float dst) {
    if (dst > uSmoothingRadius) return 0.0;
    float v = uSmoothingRadius - dst;
    float scale = 45.0 / (PI * pow(uSmoothingRadius,6.0));
    return -v*v*scale;
}

float poly6(float dst) {
    if (dst >= uSmoothingRadius) return 0.0;
    float r2 = uSmoothingRadius*uSmoothingRadius;
    float x = r2 - dst*dst;
    float scale = 315.0 / (64.0*PI*pow(uSmoothingRadius,9.0));
    return x*x*x*scale;
}

ivec3 cellCoord(vec3 p) {
    ivec3 c = ivec3(floor((p-uGridOrigin)/uCellSize));
    return clamp(c, ivec3(0), uGridDim-ivec3(1));
}

uint cellIndex(ivec3 c) {
    return uint(c.x + GRID_SIZE*(c.y + GRID_SIZE*c.z));
}

bool validCell(ivec3 c) {
    return all(greaterThanEqual(c,ivec3(0))) &&
           all(lessThan(c,uGridDim));
}

uint cellBegin(ivec3 c) {
    return cellIndex(c)*CELL_CAPACITY;
}

uint neighborCount(ivec3 c) {
    return min(cellCounts[cellIndex(c)], uint(CELL_CAPACITY));
}

float pressureFromDensity(float density) {
    return (density-uTargetDensity)*uPressureMultiplier;
}

float nearPressureFromDensity(float nearDensity) {
    return nearDensity*uNearPressureMultiplier;
}

void forNeighbors(uint i, out uint dummy) {
    dummy = i;
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uParticleCount) return;

    // --------------------------------------------------------
    // 0: prediction
    // --------------------------------------------------------
    if (uMode == 0u) {
        particles[i].vel.xyz += vec3(0,-uGravityStrength,0)*uDt;
        particles[i].predicted.xyz =
            particles[i].pos.xyz + particles[i].vel.xyz*uDt;
        return;
    }

    // --------------------------------------------------------
    // 1: build spatial grid
    // --------------------------------------------------------
    if (uMode == 1u) {
        ivec3 c = cellCoord(particles[i].predicted.xyz);
        uint cell = cellIndex(c);
        uint slot = atomicAdd(cellCounts[cell],1u);

        if (slot < uint(CELL_CAPACITY))
            cellParticles[cell*CELL_CAPACITY+slot] = i;
        return;
    }

    // --------------------------------------------------------
    // 2: density
    // --------------------------------------------------------
    if (uMode == 2u) {
        vec3 pos = particles[i].predicted.xyz;
        ivec3 base = cellCoord(pos);

        float density = 0.0;
        float nearDensity = 0.0;

        for (int dx=-1; dx<=1; ++dx)
        for (int dy=-1; dy<=1; ++dy)
        for (int dz=-1; dz<=1; ++dz) {
            ivec3 c = base + ivec3(dx,dy,dz);
            if (!validCell(c)) continue;

            uint count = neighborCount(c);
            uint begin = cellBegin(c);

            for (uint k=0u; k<count; ++k) {
                uint j = cellParticles[begin+k];
                vec3 offset = particles[j].predicted.xyz-pos;
                float sqrDst = dot(offset,offset);

                if (sqrDst > uSmoothingRadius*uSmoothingRadius) continue;

                float dst = sqrt(sqrDst);
                density += uParticleMass*spiky2(dst);
                nearDensity += uParticleMass*spiky3(dst);
            }
        }

        particles[i].densityNear.x = max(density,0.0001);
        particles[i].densityNear.y = max(nearDensity,0.0001);
        return;
    }

    // --------------------------------------------------------
    // 3: pressure + viscosity
    // --------------------------------------------------------
    if (uMode == 3u) {
        vec3 pos = particles[i].predicted.xyz;
        vec3 velocity = particles[i].vel.xyz;

        float density = max(particles[i].densityNear.x,0.0001);
        float nearDensity = max(particles[i].densityNear.y,0.0001);

        float pressure = pressureFromDensity(density);
        float nearPressure = nearPressureFromDensity(nearDensity);

        vec3 pressureForce = vec3(0);
        vec3 viscosityForce = vec3(0);

        ivec3 base = cellCoord(pos);

        for (int dx=-1; dx<=1; ++dx)
        for (int dy=-1; dy<=1; ++dy)
        for (int dz=-1; dz<=1; ++dz) {
            ivec3 c = base + ivec3(dx,dy,dz);
            if (!validCell(c)) continue;

            uint count = neighborCount(c);
            uint begin = cellBegin(c);

            for (uint k=0u; k<count; ++k) {
                uint j = cellParticles[begin+k];
                if (j == i) continue;

                vec3 offset = particles[j].predicted.xyz-pos;
                float sqrDst = dot(offset,offset);
                if (sqrDst > uSmoothingRadius*uSmoothingRadius) continue;

                float dst = sqrt(sqrDst);
                vec3 dir = dst > 0.00001 ? offset/dst : vec3(0,1,0);

                float neighbourDensity =
                    max(particles[j].densityNear.x,0.0001);
                float neighbourNearDensity =
                    max(particles[j].densityNear.y,0.0001);

                float neighbourPressure =
                    pressureFromDensity(neighbourDensity);
                float neighbourNearPressure =
                    nearPressureFromDensity(neighbourNearDensity);

                float sharedPressure =
                    0.5*(pressure+neighbourPressure);
                float sharedNearPressure =
                    0.5*(nearPressure+neighbourNearPressure);

                pressureForce += dir*dSpiky2(dst)
                    * sharedPressure*uParticleMass/neighbourDensity;

                pressureForce += dir*dSpiky3(dst)
                    * sharedNearPressure*uParticleMass/neighbourNearDensity;

                viscosityForce +=
                    (particles[j].vel.xyz-velocity)*poly6(dst);
            }
        }

        particles[i].vel.xyz +=
            (pressureForce/density)*uDt;

        particles[i].vel.xyz +=
            viscosityForce*uViscosityStrength*uDt;
        return;
    }

    // --------------------------------------------------------
    // 4: integration + collisions
    // --------------------------------------------------------
    if (uMode == 4u) {
        vec3 pos = particles[i].pos.xyz;
        vec3 vel = particles[i].vel.xyz;

        pos += vel*uDt;

        vec3 minP = uBoxMin + vec3(uParticleRadius);
        vec3 maxP = uBoxMax - vec3(uParticleRadius);

        if (pos.x < minP.x) {
            pos.x=minP.x;
            if (vel.x<0) vel.x *= -uCollisionDamping;
        } else if (pos.x > maxP.x) {
            pos.x=maxP.x;
            if (vel.x>0) vel.x *= -uCollisionDamping;
        }

        if (pos.y < minP.y) {
            pos.y=minP.y;
            if (vel.y<0) vel.y *= -uCollisionDamping;
        } else if (pos.y > maxP.y) {
            pos.y=maxP.y;
            if (vel.y>0) vel.y *= -uCollisionDamping;
        }

        if (pos.z < minP.z) {
            pos.z=minP.z;
            if (vel.z<0) vel.z *= -uCollisionDamping;
        } else if (pos.z > maxP.z) {
            pos.z=maxP.z;
            if (vel.z>0) vel.z *= -uCollisionDamping;
        }

        if (uHasObstacle != 0) {
            vec3 offset = pos-uObstaclePos;
            float dst2 = dot(offset,offset);
            float minDst = uObstacleRadius+uParticleRadius;

            if (dst2 < minDst*minDst) {
                float dst = sqrt(max(dst2,0.0000001));
                vec3 normal = dst>0.0001 ?
                    offset/dst : vec3(0,1,0);

                pos = uObstaclePos+normal*minDst;

                float vn = dot(vel,normal);
                if (vn<0)
                    vel -= (1.0+uCollisionDamping)*vn*normal;
            }
        }

        particles[i].pos.xyz = pos;
        particles[i].vel.xyz = vel;
    }
})GLSL";

void drawSimpleSphere(GLuint shader, GLuint vao, size_t indexCount,
                      const glm::vec3& position, float radius,
                      const glm::vec3& color,
                      const glm::mat4& view, const glm::mat4& projection) {
    glUseProgram(shader);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
    model = glm::scale(model, glm::vec3(radius));

    glUniformMatrix4fv(glGetUniformLocation(shader,"model"),1,GL_FALSE,glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(shader,"view"),1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader,"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(shader,"color"),1,glm::value_ptr(color));

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr);
}

void updateAndDrawEditObstacle(GLFWwindow* window, GLuint shader,
                               GLuint sphereVAO, size_t indexCount,
                               const glm::mat4& view,
                               const glm::mat4& projection) {
    if (!editMode) return;

    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    glm::vec3 ray = getRayFromMouse(mouseX, mouseY, projection, view);

    if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) sphereDepth += 3.0f * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS) sphereDepth -= 3.0f * deltaTime;
    sphereDepth = std::max(0.5f, sphereDepth);

    glm::vec3 previewPos = cameraPos + glm::normalize(ray) * sphereDepth;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        spherePos = previewPos;
        hasSphere = true;
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    drawSimpleSphere(shader, sphereVAO, indexCount, previewPos, sphereRadius,
                     hasSphere ? glm::vec3(0,1,1) : glm::vec3(1,1,0),
                     view, projection);

    if (hasSphere && glm::length(previewPos - spherePos) > 0.01f) {
        drawSimpleSphere(shader, sphereVAO, indexCount, spherePos, sphereRadius,
                         glm::vec3(0,0.8f,1), view, projection);
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

glm::vec3 getRayFromMouse(double mouseX, double mouseY,
                          const glm::mat4& projection,
                          const glm::mat4& view) {
    float x = 2.0f * static_cast<float>(mouseX) / SCR_WIDTH - 1.0f;
    float y = 1.0f - 2.0f * static_cast<float>(mouseY) / SCR_HEIGHT;

    glm::vec4 rayClip(x,y,-1,1);
    glm::vec4 rayEye = glm::inverse(projection) * rayClip;
    rayEye = glm::vec4(rayEye.x,rayEye.y,-1,0);

    return glm::normalize(glm::vec3(glm::inverse(view) * rayEye));
}

void processInput(GLFWwindow* window) {
    float cameraSpeed = 5.0f * deltaTime;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cameraPos += cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cameraPos -= cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        cameraPos -= glm::normalize(glm::cross(cameraFront,cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        cameraPos += glm::normalize(glm::cross(cameraFront,cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) cameraPos.y += cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) cameraPos.y -= cameraSpeed;

    int mState = glfwGetKey(window, GLFW_KEY_M);
    if (mState == GLFW_PRESS && !mWasPressed) {
        editMode = !editMode;
        cameraActive = false;
        firstMouse = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    mWasPressed = (mState == GLFW_PRESS);

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (!cameraActive || editMode) return;

    if (firstMouse) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
        return;
    }

    float xoffset = static_cast<float>(xpos) - lastX;
    float yoffset = lastY - static_cast<float>(ypos);
    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);

    xoffset *= 0.1f;
    yoffset *= 0.1f;

    yaw += xoffset;
    pitch += yoffset;
    pitch = std::clamp(pitch,-89.0f,89.0f);

    glm::vec3 front;
    front.x = std::cos(glm::radians(yaw))*std::cos(glm::radians(pitch));
    front.y = std::sin(glm::radians(pitch));
    front.z = std::sin(glm::radians(yaw))*std::cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS && !editMode) {
        cameraActive = !cameraActive;
        firstMouse = true;
        glfwSetInputMode(window, GLFW_CURSOR,
                         cameraActive ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
}

//shader de profondeur
GLuint createLiquidDepthShader()
{
    const char* vs = R"GLSL(
#version 430 core

struct Particle {
    vec4 pos;
    vec4 predicted;
    vec4 vel;
    vec4 color;
    vec4 densityNear;
};

layout(std430, binding = 0) readonly buffer ParticleBuffer
{
    Particle particles[];
};

uniform mat4 view;
uniform mat4 projection;
uniform float radius;

out vec3 vViewPos;
out float vRadius;

void main()
{
    vec3 worldPos = particles[gl_VertexID].pos.xyz;

    vec4 viewPos = view * vec4(worldPos, 1.0);

    gl_Position = projection * viewPos;

    vViewPos = viewPos.xyz;
    vRadius = radius;

    float distanceToCamera = max(-viewPos.z, 0.001);

    float pixelRadius =
        radius *
        projection[1][1] /
        distanceToCamera *
        0.5 *
        600.0;

    gl_PointSize = clamp(pixelRadius * 2.0, 1.0, 2048.0);
}
)GLSL";

    const char* fs = R"GLSL(
#version 430 core

in vec3 vViewPos;
in float vRadius;

layout(location = 0) out float FragDepth;

uniform mat4 projection;

void main()
{
    vec2 uv = gl_PointCoord * 2.0 - 1.0;

    float r2 = dot(uv, uv);

    if (r2 > 1.0)
        discard;

    // Surface avant d'une petite sphère.
    float sphereZ = sqrt(max(0.0, 1.0 - r2));

    // Rayon en espace caméra.
    float viewRadius = vRadius;

    // Z caméra : négatif devant la caméra.
    float viewZ =
        vViewPos.z +
        sphereZ * viewRadius;

    // Reprojection vers l'espace clip.
    vec4 clipPos =
        projection *
        vec4(vViewPos.x, vViewPos.y, viewZ, 1.0);

    float depth =
        clipPos.z / clipPos.w * 0.5 + 0.5;

    depth = clamp(depth, 0.0, 1.0);

    gl_FragDepth = depth;

    FragDepth = depth;
}
)GLSL";

    return linkProgram({
        compileShader(GL_VERTEX_SHADER, vs),
        compileShader(GL_FRAGMENT_SHADER, fs)
    });
}

// shader epaisseur
GLuint createLiquidThicknessShader()
{
    const char* vs = R"GLSL(
#version 430 core

struct Particle {
    vec4 pos;
    vec4 predicted;
    vec4 vel;
    vec4 color;
    vec4 densityNear;
};

layout(std430, binding = 0) readonly buffer ParticleBuffer
{
    Particle particles[];
};

uniform mat4 view;
uniform mat4 projection;
uniform float radius;

void main()
{
    vec3 worldPos = particles[gl_VertexID].pos.xyz;

    vec4 viewPos =
        view * vec4(worldPos, 1.0);

    gl_Position =
        projection * viewPos;

    float distanceToCamera =
        max(-viewPos.z, 0.001);

    float pixelRadius =
        radius *
        projection[1][1] /
        distanceToCamera *
        0.5 *
        600.0;

    gl_PointSize =
        clamp(pixelRadius * 2.0, 1.0, 2048.0);
}
)GLSL";

    const char* fs = R"GLSL(
#version 430 core

layout(location = 0) out float FragThickness;

void main()
{
    vec2 uv =
        gl_PointCoord * 2.0 - 1.0;

    float r2 =
        dot(uv, uv);

    if (r2 > 1.0)
        discard;

    // Plus fort au centre, doux sur les bords.
    float thickness =
        1.0 - smoothstep(
            0.45,
            1.0,
            r2
        );

    FragThickness = thickness;
}
)GLSL";

    return linkProgram({
        compileShader(GL_VERTEX_SHADER, vs),
        compileShader(GL_FRAGMENT_SHADER, fs)
    });
}

//shader de bluring
GLuint createLiquidBlurShader()
{
    const char* vs = R"GLSL(
#version 430 core

out vec2 uv;

void main()
{
    vec2 positions[3] = vec2[](
        vec2(-1.0,-1.0),
        vec2( 3.0,-1.0),
        vec2(-1.0, 3.0)
    );

    vec2 p = positions[gl_VertexID];

    uv = p * 0.5 + 0.5;

    gl_Position = vec4(p,0.0,1.0);
}
)GLSL";

    const char* fs = R"GLSL(
#version 430 core

in vec2 uv;
out vec4 FragColor;

uniform sampler2D sourceTexture;
uniform vec2 texelSize;

void main()
{
    float sum = 0.0;
    float weight = 0.0;

    for (int x = -4; x <= 4; ++x)
    {
        for (int y = -4; y <= 4; ++y)
        {
            vec2 offset =
                vec2(float(x), float(y)) * texelSize;

            float w =
                exp(
                    -float(x*x + y*y) / 12.0
                );

            sum += texture(sourceTexture, uv + offset).r * w;
            weight += w;
        }
    }

    float value = sum / max(weight, 0.0001);

    FragColor = vec4(value, value, value, 1.0);
}
)GLSL";

    return linkProgram({
        compileShader(GL_VERTEX_SHADER, vs),
        compileShader(GL_FRAGMENT_SHADER, fs)
    });
}

// real shader (celui qui est pret)
GLuint createLiquidCompositeShader()
{
    const char* vs = R"GLSL(
#version 430 core

out vec2 uv;

void main()
{
    vec2 positions[3] = vec2[](
        vec2(-1.0,-1.0),
        vec2( 3.0,-1.0),
        vec2(-1.0, 3.0)
    );

    vec2 p = positions[gl_VertexID];

    uv = p * 0.5 + 0.5;

    gl_Position = vec4(p,0.0,1.0);
}
)GLSL";

    const char* fs = R"GLSL(
#version 430 core

in vec2 uv;

out vec4 FragColor;

uniform sampler2D liquidDepth;
uniform sampler2D liquidThickness;

uniform vec3 cameraPosition;

uniform mat4 projection;
uniform mat4 invProjection;

void main()
{
    float depth = texture(liquidDepth, uv).r;

    if (depth >= 0.999999)
        discard;

    float thickness =
        texture(liquidThickness, uv).r;

    // Reconstruct view-space position.
    float z = depth * 2.0 - 1.0;

    vec4 clipPos =
        vec4(
            uv * 2.0 - 1.0,
            z,
            1.0
        );

    vec4 viewPos =
        invProjection * clipPos;

    viewPos /= viewPos.w;

    // Approximate normal from neighboring depth values.
    float dx =
        textureOffset(liquidDepth, uv, ivec2(1,0)).r -
        textureOffset(liquidDepth, uv, ivec2(-1,0)).r;

    float dy =
        textureOffset(liquidDepth, uv, ivec2(0,1)).r -
        textureOffset(liquidDepth, uv, ivec2(0,-1)).r;

    vec3 normal =
        normalize(
            vec3(-dx * 8.0, -dy * 8.0, 1.0)
        );

    // Water base.
    vec3 waterColor =
        vec3(0.03, 0.28, 0.65);

    // Fake Fresnel.
    vec3 viewDir =
        normalize(-viewPos.xyz);

    float fresnel =
        pow(
            1.0 - max(dot(normal, viewDir), 0.0),
            4.0
        );

    vec3 reflectionColor =
        vec3(0.25, 0.55, 0.85);

    vec3 color =
        mix(
            waterColor,
            reflectionColor,
            fresnel
        );

    // Thickness gives deeper blue.
    color *=
        mix(
            0.65,
            1.25,
            clamp(thickness * 2.0, 0.0, 1.0)
        );

    // Simple lighting.
    vec3 lightDir =
        normalize(vec3(-0.4, 1.0, 0.3));

    float diffuse =
        max(dot(normal, lightDir), 0.0);

    color *=
        0.55 + diffuse * 0.65;

    // Specular highlight.
    vec3 halfVector =
        normalize(lightDir + viewDir);

    float spec =
        pow(
            max(dot(normal, halfVector), 0.0),
            80.0
        );

    color +=
        vec3(0.7, 0.85, 1.0) * spec;

    float alpha =
        clamp(
            0.35 + thickness * 0.9,
            0.35,
            0.92
        );

    FragColor =
        vec4(color, alpha);
}
)GLSL";

    return linkProgram({
        compileShader(GL_VERTEX_SHADER, vs),
        compileShader(GL_FRAGMENT_SHADER, fs)
    });
}

// le buffers

void createLiquidBuffers()
{
    liquidWidth = SCR_WIDTH;
    liquidHeight = SCR_HEIGHT;

    // --------------------------------------------------------
    // Depth
    // --------------------------------------------------------

    glGenFramebuffers(1, &liquidDepthFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, liquidDepthFBO);

    glGenTextures(1, &liquidDepthTexture);
    glBindTexture(GL_TEXTURE_2D, liquidDepthTexture);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R32F,
        liquidWidth,
        liquidHeight,
        0,
        GL_RED,
        GL_FLOAT,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        liquidDepthTexture,
        0
    );

    GLuint depthBuffer;

    glGenRenderbuffers(1, &depthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);

    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH_COMPONENT24,
        liquidWidth,
        liquidHeight
    );

    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER,
        depthBuffer
    );

    GLenum drawBuffers[] = {
        GL_COLOR_ATTACHMENT0
    };

    glDrawBuffers(1, drawBuffers);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE)
    {
        std::cerr << "Liquid depth framebuffer incomplete\n";
    }

    // --------------------------------------------------------
    // Thickness
    // --------------------------------------------------------

    glGenFramebuffers(1, &liquidThicknessFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, liquidThicknessFBO);

    glGenTextures(1, &liquidThicknessTexture);
    glBindTexture(GL_TEXTURE_2D, liquidThicknessTexture);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R32F,
        liquidWidth,
        liquidHeight,
        0,
        GL_RED,
        GL_FLOAT,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        liquidThicknessTexture,
        0
    );

    glDrawBuffers(1, drawBuffers);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE)
    {
        std::cerr << "Liquid thickness framebuffer incomplete\n";
    }

    // --------------------------------------------------------
    // Blur
    // --------------------------------------------------------

    glGenFramebuffers(1, &liquidBlurFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, liquidBlurFBO);

    glGenTextures(1, &liquidBlurTexture);
    glBindTexture(GL_TEXTURE_2D, liquidBlurTexture);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R32F,
        liquidWidth,
        liquidHeight,
        0,
        GL_RED,
        GL_FLOAT,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        liquidBlurTexture,
        0
    );

    glDrawBuffers(1, drawBuffers);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE)
    {
        std::cerr << "Liquid blur framebuffer incomplete\n";
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int main() {
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return -1;
    }

    // Compute shaders + SSBO = OpenGL 4.3.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(
        SCR_WIDTH, SCR_HEIGHT, "Simulateur SPH 3D - GPU", nullptr, nullptr);

    if (!window) {
        std::cerr << "Impossible de creer la fenetre OpenGL 4.3\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    std::cout << "OpenGL : " << glGetString(GL_VERSION) << "\n";
    std::cout << "GLSL   : " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";
    std::cout << "GPU    : " << glGetString(GL_RENDERER) << "\n";

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 4 || (major == 4 && minor < 3)) {
        std::cerr << "OpenGL 4.3 minimum requis.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    glEnable(GL_DEPTH_TEST);

    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);

    GLFWwindow* window2 = glfwCreateWindow(
        460, 650, "Parametres du fluide", nullptr, window);

    if (!window2) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window2);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window2, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    glfwMakeContextCurrent(window);

    GLuint sphereShader = createRenderShader();
    GLuint simpleShader = createSimpleShader();

    GLuint computeShader = createComputeShaderFromSource(fluidComputeSource);

    liquidDepthShader =
    createLiquidDepthShader();

    liquidThicknessShader =
        createLiquidThicknessShader();

    liquidBlurShader =
        createLiquidBlurShader();

    liquidCompositeShader =
        createLiquidCompositeShader();

    createLiquidBuffers();

    glGenVertexArrays(1, &fullscreenVAO);

    if (!sphereShader || !simpleShader || !computeShader) {
        std::cerr << "Creation des shaders impossible.\n";
        return -1;
    }
    if (!liquidDepthShader ||
        !liquidThicknessShader ||
        !liquidBlurShader ||
        !liquidCompositeShader)
    {
        std::cerr << "Creation des shaders liquide impossible.\n";
        return -1;
    }

    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    generateSphere(vertices, indices, 16, 12);

    GLuint sphereVAO, sphereVBO, sphereEBO;
    glGenVertexArrays(1,&sphereVAO);
    glGenBuffers(1,&sphereVBO);
    glGenBuffers(1,&sphereEBO);

    glBindVertexArray(sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER,sphereVBO);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(unsigned int),indices.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    GLuint boxVAO, boxVBO, boxEBO;
    glGenVertexArrays(1,&boxVAO);
    glGenBuffers(1,&boxVBO);
    glGenBuffers(1,&boxEBO);

    glBindVertexArray(boxVAO);
    glBindBuffer(GL_ARRAY_BUFFER,boxVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(boxVertices),boxVertices,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,boxEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(boxIndices),boxIndices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Particle buffer: 20,000 * 80 bytes = 1.6 MB.
    GLuint particleSSBO;
    glGenBuffers(1,&particleSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,particleSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 MAX_SPHERES*sizeof(ParticleGPU),nullptr,GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,particleSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);

    // Fixed GPU grid: 64^3 cells * 128 indices ~= 134 MB.
    constexpr GLuint GRID_CELLS = 64u*64u*64u;
    constexpr GLuint CELL_CAPACITY = 128u;

    GLuint cellCountsSSBO, cellParticlesSSBO;
    glGenBuffers(1,&cellCountsSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,cellCountsSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 GRID_CELLS*sizeof(GLuint),nullptr,GL_DYNAMIC_DRAW);

    glGenBuffers(1,&cellParticlesSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,cellParticlesSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 GRID_CELLS*CELL_CAPACITY*sizeof(GLuint),nullptr,GL_DYNAMIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);

    std::vector<Sphere> spheres;
    spheres.reserve(MAX_SPHERES);

    lastFrame = static_cast<float>(glfwGetTime());

    while (!glfwWindowShouldClose(window) &&
           !glfwWindowShouldClose(window2)) {

        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = std::clamp(currentFrame-lastFrame,0.0f,0.05f);
        lastFrame = currentFrame;

        glfwPollEvents();
        processInput(window);

        glfwMakeContextCurrent(window);

        glm::mat4 view = glm::lookAt(cameraPos,cameraPos+cameraFront,cameraUp);
        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            static_cast<float>(SCR_WIDTH)/SCR_HEIGHT,
            0.1f,200.0f);

        spawnParticles(spheres,deltaTime,particleSSBO);
        simulateFluidGPU(
            computeShader,particleSSBO,cellCountsSSBO,
            cellParticlesSSBO,static_cast<GLuint>(spheres.size()),deltaTime);

        // ============================================================
        // RENDER FRAME
        // ORDER:
        // CLEAR -> LIQUID -> BOX -> OBSTACLE
        // ============================================================

        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        // ------------------------------------------------------------
        // 1. CLEAR MAIN FRAMEBUFFER
        // ------------------------------------------------------------

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glClearColor(
            0.035f,
            0.045f,
            0.075f,
            1.0f
        );

        glClear(
            GL_COLOR_BUFFER_BIT |
            GL_DEPTH_BUFFER_BIT
        );


        // ============================================================
        // 2. LIQUID DEPTH PASS
        // ============================================================

        glBindFramebuffer(
            GL_FRAMEBUFFER,
            liquidDepthFBO
        );

        glViewport(
            0,
            0,
            liquidWidth,
            liquidHeight
        );

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        glClearColor(
            1.0f,
            1.0f,
            1.0f,
            1.0f
        );

        glClear(
            GL_COLOR_BUFFER_BIT |
            GL_DEPTH_BUFFER_BIT
        );

        glUseProgram(liquidDepthShader);

        glBindBufferBase(
            GL_SHADER_STORAGE_BUFFER,
            0,
            particleSSBO
        );

        glUniformMatrix4fv(
            glGetUniformLocation(
                liquidDepthShader,
                "view"
            ),
            1,
            GL_FALSE,
            glm::value_ptr(view)
        );

        glUniformMatrix4fv(
            glGetUniformLocation(
                liquidDepthShader,
                "projection"
            ),
            1,
            GL_FALSE,
            glm::value_ptr(projection)
        );

        glUniform1f(
            glGetUniformLocation(
                liquidDepthShader,
                "radius"
            ),
            renderRadius
        );

        glDrawArrays(
            GL_POINTS,
            0,
            static_cast<GLsizei>(spheres.size())
        );


        // ============================================================
        // 3. LIQUID THICKNESS PASS
        // ============================================================

        glBindFramebuffer(
            GL_FRAMEBUFFER,
            liquidThicknessFBO
        );

        glViewport(
            0,
            0,
            liquidWidth,
            liquidHeight
        );

        glDisable(GL_DEPTH_TEST);

        glEnable(GL_BLEND);

        glBlendFunc(
            GL_ONE,
            GL_ONE
        );

        glClearColor(
            0.0f,
            0.0f,
            0.0f,
            1.0f
        );

        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(liquidThicknessShader);

        glBindBufferBase(
            GL_SHADER_STORAGE_BUFFER,
            0,
            particleSSBO
        );

        glUniformMatrix4fv(
            glGetUniformLocation(
                liquidThicknessShader,
                "view"
            ),
            1,
            GL_FALSE,
            glm::value_ptr(view)
        );

        glUniformMatrix4fv(
            glGetUniformLocation(
                liquidThicknessShader,
                "projection"
            ),
            1,
            GL_FALSE,
            glm::value_ptr(projection)
        );

        glUniform1f(
            glGetUniformLocation(
                liquidThicknessShader,
                "radius"
            ),
            renderRadius
        );

        glDrawArrays(
            GL_POINTS,
            0,
            static_cast<GLsizei>(spheres.size())
        );

        glDisable(GL_BLEND);


        // ============================================================
        // 4. BLUR DEPTH
        // ============================================================

        glBindFramebuffer(
            GL_FRAMEBUFFER,
            liquidBlurFBO
        );

        glViewport(
            0,
            0,
            liquidWidth,
            liquidHeight
        );

        glDisable(GL_DEPTH_TEST);

        glClearColor(
            1.0f,
            1.0f,
            1.0f,
            1.0f
        );

        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(liquidBlurShader);

        glActiveTexture(GL_TEXTURE0);

        glBindTexture(
            GL_TEXTURE_2D,
            liquidDepthTexture
        );

        glUniform1i(
            glGetUniformLocation(
                liquidBlurShader,
                "sourceTexture"
            ),
            0
        );

        glUniform2f(
            glGetUniformLocation(
                liquidBlurShader,
                "texelSize"
            ),
            1.0f / static_cast<float>(liquidWidth),
            1.0f / static_cast<float>(liquidHeight)
        );

        glBindVertexArray(fullscreenVAO);

        glDrawArrays(
            GL_TRIANGLES,
            0,
            3
        );


        // ============================================================
        // 5. RETURN TO MAIN FRAMEBUFFER
        // ============================================================

        glBindFramebuffer(
            GL_FRAMEBUFFER,
            0
        );

        glViewport(
            0,
            0,
            SCR_WIDTH,
            SCR_HEIGHT
        );

        glEnable(GL_DEPTH_TEST);


        // ============================================================
        // 6. LIQUID COMPOSITE
        // ============================================================
        // IMPORTANT:
        // Liquid is drawn FIRST.
        // Box and obstacle will therefore be visible on top.

        glEnable(GL_BLEND);

        glBlendFunc(
            GL_SRC_ALPHA,
            GL_ONE_MINUS_SRC_ALPHA
        );

        glUseProgram(
            liquidCompositeShader
        );


        // Depth texture
        glActiveTexture(GL_TEXTURE0);

        glBindTexture(
            GL_TEXTURE_2D,
            liquidBlurTexture
        );

        glUniform1i(
            glGetUniformLocation(
                liquidCompositeShader,
                "liquidDepth"
            ),
            0
        );


        // Thickness texture
        glActiveTexture(GL_TEXTURE1);

        glBindTexture(
            GL_TEXTURE_2D,
            liquidThicknessTexture
        );

        glUniform1i(
            glGetUniformLocation(
                liquidCompositeShader,
                "liquidThickness"
            ),
            1
        );


        // Projection
        glUniformMatrix4fv(
            glGetUniformLocation(
                liquidCompositeShader,
                "projection"
            ),
            1,
            GL_FALSE,
            glm::value_ptr(projection)
        );


        // Inverse projection
        glm::mat4 invProjection =
            glm::inverse(projection);

        glUniformMatrix4fv(
            glGetUniformLocation(
                liquidCompositeShader,
                "invProjection"
            ),
            1,
            GL_FALSE,
            glm::value_ptr(invProjection)
        );


        // Camera position
        glUniform3fv(
            glGetUniformLocation(
                liquidCompositeShader,
                "cameraPosition"
            ),
            1,
            glm::value_ptr(cameraPos)
        );


        // Fullscreen triangle
        glBindVertexArray(fullscreenVAO);

        glDrawArrays(
            GL_TRIANGLES,
            0,
            3
        );

        glDisable(GL_BLEND);


        // ============================================================
        // 7. BOX
        // ============================================================
        // Box is intentionally AFTER liquid.

        glEnable(GL_DEPTH_TEST);

        glUseProgram(simpleShader);

        glm::mat4 boxModel =
            glm::scale(
                glm::mat4(1.0f),
                glm::vec3(
                    boxSizeX,
                    boxSizeY,
                    boxSizeZ
                )
            );

        glUniformMatrix4fv(
            glGetUniformLocation(
                simpleShader,
                "model"
            ),
            1,
            GL_FALSE,
            glm::value_ptr(boxModel)
        );

        glUniformMatrix4fv(
            glGetUniformLocation(
                simpleShader,
                "view"
            ),
            1,
            GL_FALSE,
            glm::value_ptr(view)
        );

        glUniformMatrix4fv(
            glGetUniformLocation(
                simpleShader,
                "projection"
            ),
            1,
            GL_FALSE,
            glm::value_ptr(projection)
        );

        glUniform3f(
            glGetUniformLocation(
                simpleShader,
                "color"
            ),
            1.0f,
            1.0f,
            1.0f
        );

        glBindVertexArray(boxVAO);

        glDrawElements(
            GL_LINES,
            24,
            GL_UNSIGNED_INT,
            nullptr
        );


        // ============================================================
        // 8. OBSTACLE
        // ============================================================
        // Obstacle is intentionally LAST so it stays clearly visible.

        updateAndDrawEditObstacle(
            window,
            simpleShader,
            sphereVAO,
            indices.size(),
            view,
            projection
        );


        // ============================================================
        // 9. FRAME END
        // ============================================================

        glfwSwapBuffers(window);

        // ImGui.
        glfwMakeContextCurrent(window2);
        int uiWidth,uiHeight;
        glfwGetFramebufferSize(window2,&uiWidth,&uiHeight);
        glViewport(0,0,uiWidth,uiHeight);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.08f,0.08f,0.10f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Simulation");
        ImGui::Text("Particules : %d / %d",
                    static_cast<int>(spheres.size()),MAX_SPHERES);
        ImGui::Text("FPS : %.1f",ImGui::GetIO().Framerate);
        ImGui::Checkbox("Spawn automatique",&autoSpawn);
        ImGui::SliderFloat("Spawn / seconde",&spawnRate,0,1000,"%.0f");

        if (ImGui::Button("Ajouter 500 particules"))
            add500(spheres,particleSSBO);

        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            spheres.clear();
            spawnAccumulator = 0.0f;
        }
        ImGui::End();

        ImGui::Begin("SPH - Physique");
        ImGui::SliderFloat("Smoothing radius",&smoothingRadius,0.25f,2.5f,"%.3f");
        ImGui::SliderFloat("Target density",&targetDensity,0.5f,30,"%.3f");
        ImGui::SliderFloat("Pressure",&pressureMultiplier,0,100,"%.2f");
        ImGui::SliderFloat("Near pressure",&nearPressureMultiplier,0,100,"%.2f");
        ImGui::SliderFloat("Viscosite",&viscosityStrength,0,5,"%.3f");
        ImGui::SliderFloat("Masse particule",&particleMass,0.05f,5,"%.3f");
        ImGui::SliderFloat("Gravite",&gravityStrength,0,30,"%.2f");
        ImGui::SliderInt("Sous-etapes",&simulationSubsteps,1,8);
        ImGui::End();

        ImGui::Begin("Boite / collisions");
        ImGui::SliderFloat("Boite X",&boxSizeX,2,30);
        ImGui::SliderFloat("Boite Y",&boxSizeY,2,30);
        ImGui::SliderFloat("Boite Z",&boxSizeZ,2,30);
        ImGui::SliderFloat("Rebond",&collisionDamping,0,1,"%.2f");
        ImGui::End();

        ImGui::Begin("Rendu / particules");
        ImGui::SliderFloat("Rayon physique",&particleRadius,0.03f,0.5f,"%.3f");
        ImGui::SliderFloat("Rayon visible",&renderRadius,0.03f,0.5f,"%.3f");
        ImGui::TextWrapped("Le rayon visible ne change que le dessin.");
        ImGui::End();

        ImGui::Begin("Mode edition / obstacle");
        ImGui::Checkbox("Mode edition (M)",&editMode);
        ImGui::SliderFloat("Rayon obstacle",&sphereRadius,0.2f,8.0f);
        ImGui::SliderFloat("Profondeur obstacle",&sphereDepth,0.5f,30.0f);
        ImGui::Text("Z / B : eloigner / rapprocher");
        ImGui::Text("Clic gauche : placer l'obstacle");
        if (ImGui::Button("Supprimer obstacle")) hasSphere=false;
        ImGui::Text("Obstacle place : %s",hasSphere?"oui":"non");
        ImGui::End();

        ImGui::Begin("Camera");
        ImGui::Text("Clic droit : activer/desactiver camera");
        ImGui::Text("WASD : mouvement");
        ImGui::Text("Q/E : descendre/monter");
        ImGui::Text("M : mode edition");
        ImGui::Text("ESC : quitter");
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window2);
    }

    glfwMakeContextCurrent(window2);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwMakeContextCurrent(window);

    glDeleteProgram(sphereShader);
    glDeleteProgram(simpleShader);
    glDeleteProgram(computeShader);

    glDeleteBuffers(1,&particleSSBO);
    glDeleteBuffers(1,&cellCountsSSBO);
    glDeleteBuffers(1,&cellParticlesSSBO);

    glDeleteVertexArrays(1,&sphereVAO);
    glDeleteBuffers(1,&sphereVBO);
    glDeleteBuffers(1,&sphereEBO);

    glDeleteVertexArrays(1,&boxVAO);
    glDeleteBuffers(1,&boxVBO);
    glDeleteBuffers(1,&boxEBO);

    glfwDestroyWindow(window2);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
