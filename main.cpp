#include <glad/glad.h>
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

void mouse_button_callback(GLFWwindow* window, int button, int action, int /*mods*/) {
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
}
