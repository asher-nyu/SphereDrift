#include "Angel-yjc.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <random>

typedef vec3 color3;
typedef vec3 point3;

GLfloat light_pos[] = {-14.0, 12.0, -3.0, 1.0};
GLfloat shadow_color[] = {0.25, 0.25, 0.25, 0.65};

mat4 shadow_proj_matrix;

bool ShowShadow = true;

bool UseSmoothShading = true;

bool DrawSolidSphere = true;

int FogType = 0;

bool EnableGroundTexture = true;

int EnableFireworks = 0;

int EnableBlendingShadows = 1;

bool PromptSphereGeometryFromUser = false;

int EnableSphereTexture = 0;

int object_space_local_frame = 1;

int VerticalStripes = 1;

int ToggleLatticeEffect = 0;

int UprightLattice = 0;

struct Fog {
    vec4 color = vec4(0.7f, 0.7f, 0.7f, 0.5f);
    float FogStarting = 0.0f;
    float FogEnding = 18.0f;
    float ExponentialFogDensity = 0.09f;
};

Fog fog;

class FireworkParticleSystem {
public:
    int ParticleCount{};

    std::vector<vec3> ParticleArray;

    std::vector<vec3> ParticleColorArray;
    std::vector<vec3> ParticleVelocityArray;

    GLuint VertexBufferObject{};

    GLuint shaderProgram2{};

    float FireworkCycleStartTime{};

    static GLuint InitShaderFromStrings(const char *vShaderStr, const char *fShaderStr) {
        const GLuint program = glCreateProgram();
        const GLuint vShader = glCreateShader(GL_VERTEX_SHADER);
        const GLuint fShader = glCreateShader(GL_FRAGMENT_SHADER);

        glShaderSource(vShader, 1, &vShaderStr, nullptr);
        glShaderSource(fShader, 1, &fShaderStr, nullptr);

        glCompileShader(vShader);
        GLint compiled;
        glGetShaderiv(vShader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint logSize;
            glGetShaderiv(vShader, GL_INFO_LOG_LENGTH, &logSize);
            std::vector<char> log(logSize);
            glGetShaderInfoLog(vShader, logSize, nullptr, log.data());
            std::cerr << "Vertex shader compilation failed:\n" << log.data() << "\n";
            exit(EXIT_FAILURE);
        }

        glCompileShader(fShader);
        glGetShaderiv(fShader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint logSize;
            glGetShaderiv(fShader, GL_INFO_LOG_LENGTH, &logSize);
            std::vector<char> log(logSize);
            glGetShaderInfoLog(fShader, logSize, nullptr, log.data());
            std::cerr << "Fragment shader compilation failed:\n" << log.data() << "\n";
            exit(EXIT_FAILURE);
        }

        glAttachShader(program, vShader);
        glAttachShader(program, fShader);
        glLinkProgram(program);

        GLint linked;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLint logSize;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logSize);
            std::vector<char> log(logSize);
            glGetProgramInfoLog(program, logSize, nullptr, log.data());
            std::cerr << "Shader linking failed:\n" << log.data() << "\n";
            exit(EXIT_FAILURE);
        }

        glDeleteShader(vShader);
        glDeleteShader(fShader);

        return program;
    }

    void InitializeParticleData(int N) {
        ParticleCount = N;

        ParticleArray.resize(ParticleCount);

        ParticleColorArray.resize(ParticleCount);
        ParticleVelocityArray.resize(ParticleCount);

        for (int i = 0; i < ParticleCount; ++i) {
            ParticleArray[i] = vec3(0.0f, 0.1f, 0.0f);
        }

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        std::uniform_real_distribution<float> dist_signed(-1.0f, 1.0f);

        for (int i = 0; i < ParticleCount; ++i) {
            ParticleColorArray[i] = vec3(dist(gen), dist(gen), dist(gen));
        }

        for (int i = 0; i < ParticleCount; ++i) {
            const float vx = 2.0f * dist_signed(gen);
            const float vy = 2.4f * dist(gen);
            const float vz = 2.0f * dist_signed(gen);
            ParticleVelocityArray[i] = vec3(vx, vy, vz);
        }

        const auto vertexFireworksShaderSource = R"(

#version 150

in  vec3 vPosition;
in  vec3 vColor;
in  vec3 vVelocity;

out vec4 color;
out float Y;

uniform mat4 model_view;
uniform mat4 projection;
uniform float time;

void main() {

    vec4 vPosition4 = vec4(vPosition.x, vPosition.y, vPosition.z, 1.0);
    vPosition4.x = vPosition4.x + 0.001 * vVelocity.x * time;
    vPosition4.z = vPosition4.z + 0.001 * vVelocity.z * time;

    float a = -0.00000049;
    vPosition4.y = vPosition4.y + 0.001 * vVelocity.y * time + 0.5 * a * time * time;

    Y = vPosition4.y;

    vec4 FinalPosition = projection * model_view * vPosition4;
    gl_PointSize = 3.0;
    gl_Position = FinalPosition;

    color = vec4(vColor.r, vColor.g, vColor.b, 1.0);
}

)";

        const auto fragmentFireworksShaderSource = R"(

#version 150

in vec4 color;
in float Y;

out vec4 fColor;

void main() {

    if (Y < 0.1) {
        discard;
    }

    fColor = color;

}

)";

        shaderProgram2 = InitShaderFromStrings(vertexFireworksShaderSource, fragmentFireworksShaderSource);
    }

    void InitializeParticleBufferData() {
        glGenBuffers(1, &VertexBufferObject);
        glBindBuffer(GL_ARRAY_BUFFER, VertexBufferObject);
        const auto bufferSize = static_cast<GLsizeiptr>(
            sizeof(vec3) * ParticleCount * 3
        );
        glBufferData(GL_ARRAY_BUFFER, bufferSize, nullptr, GL_STATIC_DRAW);
        std::vector<vec3> VertexArray(ParticleCount);
        for (int i = 0; i < ParticleCount; ++i) {
            VertexArray[i] = ParticleArray[i];
        }
        std::vector<vec3> ColorArray(ParticleCount);
        for (int i = 0; i < ParticleCount; ++i) {
            ColorArray[i] = ParticleColorArray[i];
        }
        std::vector<vec3> VelocityArray(ParticleCount);
        for (int i = 0; i < ParticleCount; ++i) {
            VelocityArray[i] = ParticleVelocityArray[i];
        }

        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            static_cast<GLsizeiptr>(sizeof(vec3) * ParticleCount),
            VertexArray.data()
        );

        glBufferSubData(
            GL_ARRAY_BUFFER,
            static_cast<GLintptr>(sizeof(vec3) * ParticleCount),
            static_cast<GLsizeiptr>(sizeof(vec3) * ParticleCount),
            ColorArray.data()
        );

        glBufferSubData(
            GL_ARRAY_BUFFER,
            static_cast<GLintptr>(sizeof(vec3) * ParticleCount * 2),
            static_cast<GLsizeiptr>(sizeof(vec3) * ParticleCount),
            VelocityArray.data()
        );
    }

    void DrawFireworkParticles() const {
        glBindBuffer(GL_ARRAY_BUFFER, VertexBufferObject);

        const GLuint vPosition = glGetAttribLocation(shaderProgram2, "vPosition");
        glEnableVertexAttribArray(vPosition);
        glVertexAttribPointer(vPosition, 3, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(nullptr));

        const GLuint vColor = glGetAttribLocation(shaderProgram2, "vColor");
        glEnableVertexAttribArray(vColor);
        glVertexAttribPointer(vColor, 3, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(sizeof(vec3) * ParticleCount));

        const GLuint vVelocity = glGetAttribLocation(shaderProgram2, "vVelocity");
        glEnableVertexAttribArray(vVelocity);
        glVertexAttribPointer(vVelocity, 3, GL_FLOAT, GL_FALSE, 0,
                              BUFFER_OFFSET(sizeof(vec3) * ParticleCount + sizeof(vec3) * ParticleCount));

        glDrawArrays(GL_POINTS, 0, ParticleCount);

        glDisableVertexAttribArray(vPosition);
        glDisableVertexAttribArray(vColor);
        glDisableVertexAttribArray(vVelocity);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
};

FireworkParticleSystem fireworks;

#define ImageWidth  32
#define ImageHeight 32
GLubyte Image[ImageHeight][ImageWidth][4];

#define	stripeImageWidth 32
GLubyte stripeImage[4 * stripeImageWidth];

void image_set_up() {
    int j;

    for (int i = 0; i < ImageHeight; i++)
        for (j = 0; j < ImageWidth; j++) {
            int c = (((i & 0x8) == 0) ^ ((j & 0x8) == 0));

            if (c == 1)
            {
                c = 255;
                Image[i][j][0] = static_cast<GLubyte>(c);
                Image[i][j][1] = static_cast<GLubyte>(c);
                Image[i][j][2] = static_cast<GLubyte>(c);
            } else
            {
                Image[i][j][0] = static_cast<GLubyte>(0);
                Image[i][j][1] = static_cast<GLubyte>(150);
                Image[i][j][2] = static_cast<GLubyte>(0);
            }

            Image[i][j][3] = static_cast<GLubyte>(255);
        }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (j = 0; j < stripeImageWidth; j++) {
        stripeImage[4 * j] = static_cast<GLubyte>(255);
        stripeImage[4 * j + 1] = static_cast<GLubyte>((j > 4) ? 255 : 0);
        stripeImage[4 * j + 2] = static_cast<GLubyte>(0);
        stripeImage[4 * j + 3] = static_cast<GLubyte>(255);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

}

GLuint InitShaderFromStrings(const char *vShaderStr, const char *fShaderStr) {
    const GLuint program = glCreateProgram();
    const GLuint vShader = glCreateShader(GL_VERTEX_SHADER);
    const GLuint fShader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vShader, 1, &vShaderStr, nullptr);
    glShaderSource(fShader, 1, &fShaderStr, nullptr);

    glCompileShader(vShader);
    GLint compiled;
    glGetShaderiv(vShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint logSize;
        glGetShaderiv(vShader, GL_INFO_LOG_LENGTH, &logSize);
        std::vector<char> log(logSize);
        glGetShaderInfoLog(vShader, logSize, nullptr, log.data());
        std::cerr << "Vertex shader compilation failed:\n" << log.data() << "\n";
        exit(EXIT_FAILURE);
    }

    glCompileShader(fShader);
    glGetShaderiv(fShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint logSize;
        glGetShaderiv(fShader, GL_INFO_LOG_LENGTH, &logSize);
        std::vector<char> log(logSize);
        glGetShaderInfoLog(fShader, logSize, nullptr, log.data());
        std::cerr << "Fragment shader compilation failed:\n" << log.data() << "\n";
        exit(EXIT_FAILURE);
    }

    glAttachShader(program, vShader);
    glAttachShader(program, fShader);
    glLinkProgram(program);

    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint logSize;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logSize);
        std::vector<char> log(logSize);
        glGetProgramInfoLog(program, logSize, nullptr, log.data());
        std::cerr << "Shader linking failed:\n" << log.data() << "\n";
        exit(EXIT_FAILURE);
    }

    glDeleteShader(vShader);
    glDeleteShader(fShader);

    return program;
}

bool ReachedDestination(const point3 &start, const point3 &current, const point3 &end) {
    float totalPathLength = length(end - start);
    float traveled = length(current - start);
    return traveled >= totalPathLength;
}

class Object3D {
public:
    std::vector<point3> vertices;
    std::vector<color3> colors;
    std::vector<vec3> normals;
    std::vector<vec2> TextureCoordinates;

    GLuint texture{};

    bool texture_enabled = false;

    int TriangleCount;
    int VertexCount{};

    GLuint VertexBufferObject{};

    bool DrawWireframe = false;

public:
    Object3D() : TriangleCount(0) {
    }

    void clear() {
        vertices.clear();
        colors.clear();
        TextureCoordinates.clear();
        TriangleCount = 0;
    }

    void ComputeNormals(bool smoothShading) {
        normals.resize(vertices.size());

        if (smoothShading) {
            for (size_t i = 0; i < vertices.size(); ++i) {
                normals[i] = normalize(vertices[i]);
            }
        } else {
            for (int i = 0; i < TriangleCount; ++i) {
                const vec3 &v0 = vertices[i * 3];
                const vec3 &v1 = vertices[i * 3 + 1];
                const vec3 &v2 = vertices[i * 3 + 2];

                vec3 normal = normalize(cross(v1 - v0, v2 - v0));
                normals[i * 3 + 0] = normal;
                normals[i * 3 + 1] = normal;
                normals[i * 3 + 2] = normal;
            }
        }
    }

    void LoadGeometryData(const char *SourceFile) {
        clear();

        std::ifstream InputStream(SourceFile);

        if (!InputStream.is_open()) {
            printf("Failed to open file: %s\n", SourceFile);
            return;
        }

        std::string line;
        if (!std::getline(InputStream, line)) {
            printf("Error: File is empty or unreadable.\n");
            return;
        }

        try {
            TriangleCount = std::stoi(line);
        } catch (...) {
            printf("Error: First line is not a valid triangle count.\n");
            return;
        }

        vertices.resize(TriangleCount * 3);

        bool ReadingVertexData = false;
        int VertexCount = 0;
        int GroupVertexCount = 0;
        int OverallVertexIndex = 0;

        while (std::getline(InputStream, line)) {
            if (line.empty()) continue;

            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (line.empty()) continue;

            if (!ReadingVertexData) {
                try {
                    VertexCount = std::stoi(line);
                    ReadingVertexData = true;
                } catch (...) {
                    printf("Warning: Skipping invalid line (expected vertex count): '%s'\n", line.c_str());
                    continue;
                }
            } else {
                std::stringstream line_stream(line);
                float x, y, z;

                if (!(line_stream >> x >> y >> z)) {
                    printf("Warning: Skipping invalid vertex line: '%s'\n", line.c_str());
                    continue;
                }

                if (OverallVertexIndex < vertices.size()) {
                    vertices[OverallVertexIndex] = point3(x, y, z);
                    ++GroupVertexCount;
                    ++OverallVertexIndex;

                    if (GroupVertexCount == VertexCount) {
                        GroupVertexCount = 0;
                        ReadingVertexData = false;
                    }
                } else {
                    printf("Warning: Too many vertices, skipping.\n");
                    break;
                }
            }
        }
    }

    void ApplyUniformColor(const color3 &DesiredColor) {
        colors.resize(TriangleCount * 3);

        for (auto &color: colors) {
            color = DesiredColor;
        }
    }

    void InitializeOpenGLBufferData() {
        if (TriangleCount > 0)
            VertexCount = TriangleCount * 3;
        else
            VertexCount = static_cast<int>(vertices.size());

        if (normals.size() != VertexCount)
            normals.resize(VertexCount, vec3(0.0f, 1.0f, 0.0f));

        glGenBuffers(1, &VertexBufferObject);
        glBindBuffer(GL_ARRAY_BUFFER, VertexBufferObject);

        if (!texture_enabled) {
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(
                    sizeof(point3) * VertexCount +
                    sizeof(color3) * VertexCount +
                    sizeof(vec3) * VertexCount
                ),
                nullptr,
                GL_STATIC_DRAW
            );
        } else {
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(
                    sizeof(point3) * VertexCount +
                    sizeof(color3) * VertexCount +
                    sizeof(vec3) * VertexCount +
                    sizeof(vec2) * VertexCount
                ),
                nullptr,
                GL_STATIC_DRAW
            );
        }
        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            static_cast<GLsizeiptr>(sizeof(point3) * VertexCount),
            vertices.data()
        );
        glBufferSubData(GL_ARRAY_BUFFER,
                        static_cast<GLintptr>(sizeof(point3) * VertexCount),
                        static_cast<GLsizeiptr>(sizeof(color3) * VertexCount),
                        colors.data());

        glBufferSubData(
            GL_ARRAY_BUFFER,
            static_cast<GLintptr>(sizeof(point3) * VertexCount + sizeof(color3) * VertexCount),
            static_cast<GLsizeiptr>(sizeof(vec3) * VertexCount),
            normals.data()
        );

        if (texture_enabled) {
            const std::vector<vec2> TextureCoordinateArray(VertexCount);

            const auto textureOffset = static_cast<GLintptr>(
                sizeof(point3) * VertexCount +
                sizeof(color3) * VertexCount +
                sizeof(vec3) * VertexCount
            );
            const auto textureSize = static_cast<GLsizeiptr>(sizeof(vec2) * VertexCount);

            if (TextureCoordinates.empty()) {
                glBufferSubData(
                    GL_ARRAY_BUFFER,
                    textureOffset,
                    textureSize,
                    TextureCoordinateArray.data()
                );
            } else {
                glBufferSubData(
                    GL_ARRAY_BUFFER,
                    textureOffset,
                    textureSize,
                    TextureCoordinates.data()
                );
            }
        }
    }

    void draw(const GLuint shaderProgram1) const {
        glBindBuffer(GL_ARRAY_BUFFER, VertexBufferObject);

        const GLuint vPosition = glGetAttribLocation(shaderProgram1, "vPosition");
        glEnableVertexAttribArray(vPosition);
        glVertexAttribPointer(vPosition, 3, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(nullptr));

        const GLuint vColor = glGetAttribLocation(shaderProgram1, "vColor");
        glEnableVertexAttribArray(vColor);
        glVertexAttribPointer(vColor, 3, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(sizeof(point3) * VertexCount));

        const GLuint vNormal = glGetAttribLocation(shaderProgram1, "vNormal");
        glEnableVertexAttribArray(vNormal);
        glVertexAttribPointer(vNormal, 3, GL_FLOAT, GL_FALSE, 0,
                              BUFFER_OFFSET(sizeof(point3) * VertexCount + sizeof(color3) * VertexCount));

        GLuint vTextureCoordinates = 0;

        if (texture_enabled) {
            vTextureCoordinates = glGetAttribLocation(shaderProgram1, "vTextureCoordinates");
            glEnableVertexAttribArray(vTextureCoordinates);
            glVertexAttribPointer(vTextureCoordinates, 2, GL_FLOAT, GL_FALSE, 0,
                                  BUFFER_OFFSET(
                                      sizeof(point3) * VertexCount + sizeof(color3) * VertexCount + sizeof(vec3) *
                                      VertexCount));
        }

        if (!DrawWireframe)
            glDrawArrays(GL_TRIANGLES, 0, VertexCount);
        else
            glDrawArrays(GL_LINES, 0, VertexCount);

        glDisableVertexAttribArray(vPosition);
        glDisableVertexAttribArray(vColor);
        glDisableVertexAttribArray(vNormal);

        if (texture_enabled) {
            glDisableVertexAttribArray(vTextureCoordinates);
        }
    }

    void print() const {
        if (TriangleCount == 0) {
            return;
        }

        std::cout << "Mesh Data Summary\n";
        std::cout << "Triangle Count: " << TriangleCount << "\n";
        std::cout << "Vertex Count: " << TriangleCount * 3 << "\n";

        std::cout << "Listing Triangles with Their Vertices:\n";

        for (int i = 0; i < TriangleCount; ++i) {
            std::cout << "Triangle " << i + 1 << ":\n";

            const vec3 VertexOne = vertices[(i * 3) + 0];
            const vec3 VertexTwo = vertices[(i * 3) + 1];
            const vec3 VertexThree = vertices[(i * 3) + 2];

            std::cout << "(" << VertexOne.x << "," << VertexOne.y << "," << VertexOne.z << ")\n";
            std::cout << "(" << VertexTwo.x << "," << VertexTwo.y << "," << VertexTwo.z << ")\n";
            std::cout << "(" << VertexThree.x << "," << VertexThree.y << "," << VertexThree.z << ")\n";
        }

        std::cout << std::endl;
    }
};

void promptMeshFromUser(Object3D &object3D) {
    std::string GeometryFilePath;
    std::cout << "Specify the file location for the mesh data:\n";
    std::cin >> GeometryFilePath;

    object3D.LoadGeometryData(GeometryFilePath.c_str());
    object3D.print();
}

GLuint shaderProgram1;

GLfloat fovy = 45.0;
GLfloat aspect;
GLfloat Z_Near_Plane = 0.5, Z_Far_Plane = 100.0;

vec4 InitialCameraPosition(7.0f, 3.0f, -10.0f, 1.0f);

vec4 eye = InitialCameraPosition;

Object3D CreateQuadrilateralObject3D() {
    Object3D QuadrilateralObject;

    QuadrilateralObject.TriangleCount = 2;
    QuadrilateralObject.vertices.resize(QuadrilateralObject.TriangleCount * 3);

    QuadrilateralObject.vertices[0] = point3(5.0f, 0.0f, 8.0f);
    QuadrilateralObject.vertices[1] = point3(5.0f, 0.0f, -4.0f);
    QuadrilateralObject.vertices[2] = point3(-5.0f, 0.0f, -4.0f);
    QuadrilateralObject.vertices[3] = point3(-5.0f, 0.0f, -4.0f);
    QuadrilateralObject.vertices[4] = point3(-5.0f, 0.0f, 8.0f);
    QuadrilateralObject.vertices[5] = point3(5.0f, 0.0f, 8.0f);

    QuadrilateralObject.ApplyUniformColor(color3(0.0f, 1.0f, 0.0f));

    QuadrilateralObject.texture_enabled = true;

    image_set_up();

    glGenTextures(1, &QuadrilateralObject.texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, QuadrilateralObject.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ImageWidth, ImageHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, Image);

    QuadrilateralObject.TextureCoordinates.resize(QuadrilateralObject.vertices.size());

    QuadrilateralObject.TextureCoordinates[0] = vec2(1.0f * 5.0f, 1.0f * 6.0f);
    QuadrilateralObject.TextureCoordinates[1] = vec2(1.0f * 5.0f, 0.0f * 6.0f);
    QuadrilateralObject.TextureCoordinates[2] = vec2(0.0f * 5.0f, 0.0f * 6.0f);
    QuadrilateralObject.TextureCoordinates[3] = vec2(0.0f * 5.0f, 0.0f * 6.0f);
    QuadrilateralObject.TextureCoordinates[4] = vec2(0.0f * 5.0f, 1.0f * 6.0f);
    QuadrilateralObject.TextureCoordinates[5] = vec2(1.0f * 5.0f, 1.0f * 6.0f);

    return QuadrilateralObject;
}

Object3D CreateAxisObject3D(const point3 &direction, const color3 &color) {
    Object3D axis;
    axis.TriangleCount = 0;
    axis.DrawWireframe = true;

    axis.vertices.resize(2);
    axis.colors.resize(2);
    axis.VertexCount = 2;

    axis.vertices[0] = point3(0.0f, 0.02f, 0.0f);

    axis.vertices[1] = direction;

    axis.colors[0] = color;
    axis.colors[1] = color;

    return axis;
}

Object3D GroundPlane;
Object3D xAxisLine;
Object3D yAxisLine;
Object3D zAxisLine;
Object3D RenderTargetObject;

point3 SphereReferenceLocationA(-4.0f, 1.0f, 4.0f);
point3 SphereReferenceLocationB(3.0f, 1.0f, -4.0f);
point3 SphereReferenceLocationC(-3.0f, 1.0f, -3.0f);

point3 CurrentSphereLocation = SphereReferenceLocationA;

point3 SpherePathStart = SphereReferenceLocationA;

float CurrentRotationAngle = 0.0f;
float IncrementalAngularSpeed = 3.0f;
vec3 SphereAxisForRotation;

char CurrentDestinationLabel = 'B';

mat4 SphereTranslationMatrix = Translate(CurrentSphereLocation);
auto SphereRotationMatrix = mat4();

bool ExecuteRolling = false;

void SmoothRollingMode() {
    point3 TargetLocation = SphereReferenceLocationA;
    if (CurrentDestinationLabel == 'A') {
        TargetLocation = SphereReferenceLocationA;
    } else if (CurrentDestinationLabel == 'B') {
        TargetLocation = SphereReferenceLocationB;
    } else if (CurrentDestinationLabel == 'C') {
        TargetLocation = SphereReferenceLocationC;
    }

    vec3 MovementDirection = TargetLocation - CurrentSphereLocation;

    if (ReachedDestination(SpherePathStart, CurrentSphereLocation, TargetLocation)) {
        CurrentSphereLocation = TargetLocation;
        SpherePathStart = TargetLocation;

        if (CurrentDestinationLabel == 'A') {
            CurrentDestinationLabel = 'B';
        } else if (CurrentDestinationLabel == 'B') {
            CurrentDestinationLabel = 'C';
        } else if (CurrentDestinationLabel == 'C') {
            CurrentDestinationLabel = 'A';
        }
    }

    const auto UpwardDirection = vec3(0.0f, 1.0f, 0.0f);
    vec3 SphereAxisForRotation = cross(MovementDirection, UpwardDirection);
    SphereAxisForRotation = -1.0f * normalize(SphereAxisForRotation);
    const mat4 IncrementalRotationMatrix = Rotate(IncrementalAngularSpeed, SphereAxisForRotation.x,
                                                  SphereAxisForRotation.y, SphereAxisForRotation.z);
    SphereRotationMatrix = IncrementalRotationMatrix * SphereRotationMatrix;

    MovementDirection = normalize(MovementDirection);
    const float MovementSpeed = IncrementalAngularSpeed * (2.0f * static_cast<float>(M_PI) / 360.0f);
    CurrentSphereLocation = CurrentSphereLocation + (MovementDirection * MovementSpeed);

    SphereTranslationMatrix = Translate(CurrentSphereLocation);
}

bool RollingStarted = false;
bool RollingPaused = false;

void init() {
    GroundPlane = CreateQuadrilateralObject3D();
    GroundPlane.ComputeNormals(false);

    GroundPlane.InitializeOpenGLBufferData();

    xAxisLine = CreateAxisObject3D(point3(10.0f, 0.0f, 0.0f), color3(1.0f, 0.0f, 0.0f));
    yAxisLine = CreateAxisObject3D(point3(0.0f, 10.0f, 0.0f), color3(1.0f, 0.0f, 1.0f));
    zAxisLine = CreateAxisObject3D(point3(0.0f, 0.0f, 10.0f), color3(0.0f, 0.0f, 1.0f));

    xAxisLine.InitializeOpenGLBufferData();
    yAxisLine.InitializeOpenGLBufferData();
    zAxisLine.InitializeOpenGLBufferData();

    if (PromptSphereGeometryFromUser) {
        std::string filepath;
        std::cout << "Enter the full absolute path to the sphere geometry file: ";
        std::getline(std::cin >> std::ws, filepath);
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cout << "Error: Could not open the file at the specified path.\n";
            exit(EXIT_FAILURE);
        }
        std::string headerLine;
        if (!std::getline(file, headerLine)) {
            std::cout << "Error: The file is empty or unreadable.\n";
            exit(EXIT_FAILURE);
        }
        try {
            const int triangleCount = std::stoi(headerLine);
            if (triangleCount <= 0) {
                std::cout << "Error: The file does not contain a valid triangle count.\n";
                exit(EXIT_FAILURE);
            }
        } catch (...) {
            std::cout << "Error: The file is not formatted correctly for this program.\n";
            exit(EXIT_FAILURE);
        }

        file.close();
        RenderTargetObject.LoadGeometryData(filepath.c_str());
    } else {
        RenderTargetObject.LoadGeometryData("../Data Files/sphere.1024.txt");
    }

    RenderTargetObject.ComputeNormals(UseSmoothShading);

    RenderTargetObject.ApplyUniformColor(color3(1.0f, 0.84f, 0.0f));

    image_set_up();

    RenderTargetObject.texture_enabled = true;

    glEnable(GL_TEXTURE_1D);
    glGenTextures(1, &RenderTargetObject.texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_1D, RenderTargetObject.texture);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, stripeImageWidth, 0, GL_RGBA, GL_UNSIGNED_BYTE, stripeImage);

    RenderTargetObject.InitializeOpenGLBufferData();

    fireworks.InitializeParticleData(300);
    fireworks.InitializeParticleBufferData();
    fireworks.FireworkCycleStartTime = static_cast<float>(glutGet(GLUT_ELAPSED_TIME));

    glEnable(GL_PROGRAM_POINT_SIZE);

    const auto *vertexShaderSource = R"(

#version 150

in vec4 vPosition;
in vec3 vNormal;
in vec4 vColor;
in  vec2 vTextureCoordinates;
out vec2 LatticeTextureCoordinates;

out vec4 position_of_eye;
out vec2 TextureCoordinates;

uniform mat4 model_view;
uniform mat4 projection;

uniform vec4 dir_light_position;
uniform vec4 dir_ambient_product;
uniform vec4 dir_diffuse_product;
uniform vec4 dir_specular_product;

uniform vec4 pos_light_position;
uniform vec4 pos_ambient_product;
uniform vec4 pos_diffuse_product;
uniform vec4 pos_specular_product;

uniform float shininess;

uniform bool lighting_enabled;
uniform vec4 override_color;

uniform float constant_attenuation;
uniform float linear_attenuation;
uniform float quadratic_attenuation;

uniform vec3 spot_direction;
uniform float spot_exponent;
uniform float spot_cutoff;

uniform int SphereTextureMode;
uniform int SphereTextureUseObjectSpace;
uniform int SphereTextureUseVerticalMapping;

uniform int ToggleLatticeEffect;
uniform int UprightLattice;

uniform bool point_light_enabled;

out vec4 color;

void main() {
    vec3 pos = (model_view * vPosition).xyz;
    position_of_eye = (model_view * vPosition);

if (lighting_enabled) {
    vec3 N = normalize(mat3(model_view) * vNormal);
    vec3 E = normalize(-pos);

    vec3 L1 = normalize(-dir_light_position.xyz);
    vec3 H1 = normalize(L1 + E);

    vec4 ambient1 = dir_ambient_product * vColor;
    vec4 diffuse1 = dir_diffuse_product * max(dot(L1, N), 0.0) * vColor;
    vec4 specular1 = dir_specular_product * pow(max(dot(N, H1), 0.0), shininess);
    if (dot(L1, N) < 0.0) specular1 = vec4(0.0);

vec4 light_contrib = ambient1 + diffuse1 + specular1;

if (point_light_enabled) {

    vec3 L2 = normalize(pos_light_position.xyz - pos);
    float dist = length(pos_light_position.xyz - pos);
    float attenuation = 1.0 / (constant_attenuation + linear_attenuation * dist + quadratic_attenuation * dist * dist);

    float spot_effect = 1.0;

    vec3 H2 = normalize(L2 + E);

    vec4 ambient2 = pos_ambient_product * vColor;
    vec4 diffuse2 = pos_diffuse_product * max(dot(L2, N), 0.0) * vColor;
    vec4 specular2 = pos_specular_product * pow(max(dot(N, H2), 0.0), shininess);
    if (dot(L2, N) < 0.0) specular2 = vec4(0.0);

    light_contrib += attenuation * spot_effect * (ambient2 + diffuse2 + specular2);
}
    color = light_contrib;
    color.a = 1.0;
}
else {

if (override_color.a > 0.0)
        color = override_color;
    else
        color = vColor;
    }

    gl_Position = projection * model_view * vPosition;

    TextureCoordinates = vTextureCoordinates;

    if (SphereTextureMode == 1) {

        vec3 SpherePosition = vPosition.xyz;

        if (SphereTextureUseObjectSpace == 0) {
            SpherePosition = pos.xyz;
        }

        if (SphereTextureUseVerticalMapping == 1) {
            TextureCoordinates = vec2(2.5 * SpherePosition.x, 0.0);
        } else {
            TextureCoordinates = vec2(1.5 * (SpherePosition.x + SpherePosition.y + SpherePosition.z), 0.0);
        }

    }

    else if (SphereTextureMode == 2) {

        vec3 SpherePosition = vPosition.xyz;

        if (SphereTextureUseObjectSpace == 0) {
            SpherePosition = pos.xyz;
        }

        if (SphereTextureUseVerticalMapping == 1) {
            TextureCoordinates = vec2(0.75 * (SpherePosition.x + 1), 0.75 * (SpherePosition.y + 1));
        } else {
            TextureCoordinates = vec2(0.45 * (SpherePosition.x + SpherePosition.y + SpherePosition.z), 0.45 * (SpherePosition.x - SpherePosition.y + SpherePosition.z));
        }

    }

    if (ToggleLatticeEffect == 1) {

        vec3 SpherePosition = vPosition.xyz;

        if (SphereTextureUseObjectSpace == 0) {
            SpherePosition = pos.xyz;
        }

        if (UprightLattice == 1) {
            LatticeTextureCoordinates = vec2(0.5 * (SpherePosition.x + 1), 0.5 * (SpherePosition.y + 1));
        } else {
            LatticeTextureCoordinates = vec2(0.3 * (SpherePosition.x + SpherePosition.y + SpherePosition.z), 0.3 * (SpherePosition.x - SpherePosition.y + SpherePosition.z));
        }

    }

}

)";

    const auto *fragmentShaderSource = R"(

#version 150

in vec4 color;
in vec4 position_of_eye;
in vec2 TextureCoordinates;
in vec2 LatticeTextureCoordinates;

out vec4 fColor;

uniform int texture_enabled;

uniform sampler2D texture_mapping_2D;
uniform sampler1D texture_mapping_1D;

uniform vec4 FogColor;

uniform float FogStarting;
uniform float FogEnding;

uniform float ExponentialFogDensity;

uniform int FogType;

uniform int SphereTextureMode;
uniform int ToggleLatticeEffect;

uniform bool point_light_enabled;

void main() {

    vec4 FinalColor = color;

    if (texture_enabled == 1) {

        FinalColor = color * texture(texture_mapping_2D, TextureCoordinates);

        if (SphereTextureMode == 1) {
            FinalColor = color * texture(texture_mapping_1D, TextureCoordinates.x).rgba;
        }

        else if (SphereTextureMode == 2) {

            vec4 CheckerboardTextureColor = texture(texture_mapping_2D, TextureCoordinates).rgba;

            if (CheckerboardTextureColor.r < 0.9) {
                CheckerboardTextureColor = vec4(0.9, 0.1, 0.1, 1.0);
            }

            FinalColor = color * CheckerboardTextureColor;

        }

    }

    if (ToggleLatticeEffect == 1) {

        if (fract(4 * LatticeTextureCoordinates.s) < 0.35 && fract(4 * LatticeTextureCoordinates.t) < 0.35) {
            discard;
        }

    }

    float dist = abs(position_of_eye.z / position_of_eye.w);

    switch (FogType) {
        case 1:
            {
                float f = (FogEnding - dist) / (FogEnding - FogStarting);
                f = clamp(f, 0.0, 1.0);
                FinalColor = mix( FogColor, FinalColor, f );
            }
            break;
        case 2:
            {
                float f = exp( -(ExponentialFogDensity * dist) );
                f = clamp(f, 0.0, 1.0);
                FinalColor = mix( FogColor, FinalColor, f );
            }
            break;
        case 3:
            {
                float f = exp( -(ExponentialFogDensity * dist * ExponentialFogDensity * dist) );
                f = clamp(f, 0.0, 1.0);
                FinalColor = mix( FogColor, FinalColor, f );
            }
            break;
    }

    fColor = FinalColor;

}

)";

    shaderProgram1 = InitShaderFromStrings(vertexShaderSource, fragmentShaderSource);

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.529f, 0.807f, 0.92f, 0.0f);
    glLineWidth(2.0);

    GLfloat Lx = light_pos[0];
    GLfloat Ly = light_pos[1];
    GLfloat Lz = light_pos[2];
    GLfloat Lw = light_pos[3];

    shadow_proj_matrix = mat4(
        Ly, 0.0f, 0.0f, 0.0f,
        -Lx, 0.0f, -Lz, 0.0f,
        0.0f, 0.0f, Ly, 0.0f,
        0.0f, 0.0f, -Lw, Ly
    );
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shaderProgram1);

    glUniform1i(glGetUniformLocation(shaderProgram1, "lighting_enabled"), 1);
    glUniform1i(glGetUniformLocation(shaderProgram1, "SphereTextureMode"), 0);

    const GLint ModelView = glGetUniformLocation(shaderProgram1, "model_view");
    const GLint Projection = glGetUniformLocation(shaderProgram1, "projection");

    mat4 PerspectiveMatrix = Perspective(fovy, aspect, Z_Near_Plane, Z_Far_Plane);
    glUniformMatrix4fv(Projection, 1, GL_TRUE, PerspectiveMatrix);

    const vec4 at(0.0, 0.0, 0.0, 1.0);
    const vec4 up(0.0, 1.0, 0.0, 0.0);

    mat4 ModelViewMatrix = LookAt(eye, at, up);

    vec4 light_diffuse, light_specular;

    if (EnableFireworks == 1) {
        glUseProgram(fireworks.shaderProgram2);

        float fireworksTime = static_cast<float>(glutGet(GLUT_ELAPSED_TIME)) - fireworks.FireworkCycleStartTime;

        if (fireworksTime * 0.001f > 9.0f) {
            fireworks.FireworkCycleStartTime = static_cast<float>(glutGet(GLUT_ELAPSED_TIME));
            fireworksTime = 0.0f;
        }

        glUniform1f(glGetUniformLocation(fireworks.shaderProgram2, "time"), fireworksTime);

        glUniformMatrix4fv(glGetUniformLocation(fireworks.shaderProgram2, "model_view"), 1, GL_TRUE, ModelViewMatrix);
        glUniformMatrix4fv(glGetUniformLocation(fireworks.shaderProgram2, "projection"), 1, GL_TRUE, PerspectiveMatrix);

        glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
        fireworks.DrawFireworkParticles();

        glUseProgram(shaderProgram1);
    }

    light_diffuse = vec4(0.8, 0.8, 0.8, 1.0);
    light_specular = vec4(0.2, 0.2, 0.2, 1.0);

    vec4 light_pos;
    light_pos = vec4(-14.0, 12.0, -3.0, 1.0);

    vec4 pos_light_eye = ModelViewMatrix * light_pos;

    glUniform4fv(glGetUniformLocation(shaderProgram1, "pos_light_position"), 1, pos_light_eye);

    glUniform1f(glGetUniformLocation(shaderProgram1, "constant_attenuation"), 2.0f);
    glUniform1f(glGetUniformLocation(shaderProgram1, "linear_attenuation"), 0.01f);
    glUniform1f(glGetUniformLocation(shaderProgram1, "quadratic_attenuation"), 0.001f);

    glUniform3fv(glGetUniformLocation(shaderProgram1, "spot_direction"), 1, vec3(0.0f, -1.0f, 0.0f));
    glUniform1f(glGetUniformLocation(shaderProgram1, "spot_exponent"), 0.0f);
    glUniform1f(glGetUniformLocation(shaderProgram1, "spot_cutoff"), 180.0f);

    vec4 global_ambient(1.0, 1.0, 1.0, 1.0);

    vec4 material_ambient(1.0, 1.0, 1.0, 1.0);
    vec4 material_diffuse(1.0, 1.0, 1.0, 1.0);

    vec4 material_specular(1.0, 1.0, 1.0, 1.0);

    vec4 dir_light_position(0.1, 0.0, -1.0, 0.0);
    vec4 dir_ambient(0.0, 0.0, 0.0, 1.0);
    vec4 dir_diffuse(0.8, 0.8, 0.8, 1.0);
    vec4 dir_specular(0.2, 0.2, 0.2, 1.0);

    glUniform4fv(glGetUniformLocation(shaderProgram1, "dir_light_position"), 1, dir_light_position);
    glUniform4fv(glGetUniformLocation(shaderProgram1, "dir_ambient_product"), 1, dir_ambient * material_ambient);
    glUniform4fv(glGetUniformLocation(shaderProgram1, "dir_diffuse_product"), 1, dir_diffuse * material_diffuse);
    glUniform4fv(glGetUniformLocation(shaderProgram1, "dir_specular_product"), 1, dir_specular * material_specular);

    vec4 pos_light = vec4(-14.0, 12.0, -3.0, 1.0);
    vec4 pos_ambient = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 pos_diffuse = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 pos_specular = vec4(1.0, 1.0, 1.0, 1.0);

    glUniform4fv(glGetUniformLocation(shaderProgram1, "pos_light_position"), 1, ModelViewMatrix * pos_light);
    glUniform4fv(glGetUniformLocation(shaderProgram1, "pos_ambient_product"), 1, pos_ambient * material_ambient);
    glUniform4fv(glGetUniformLocation(shaderProgram1, "pos_diffuse_product"), 1, pos_diffuse * material_diffuse);
    glUniform4fv(glGetUniformLocation(shaderProgram1, "pos_specular_product"), 1, pos_specular * material_specular);

    glUniform1i(glGetUniformLocation(shaderProgram1, "point_light_enabled"), 1);

    glUniform1f(glGetUniformLocation(shaderProgram1, "constant_attenuation"), 2.0f);
    glUniform1f(glGetUniformLocation(shaderProgram1, "linear_attenuation"), 0.01f);
    glUniform1f(glGetUniformLocation(shaderProgram1, "quadratic_attenuation"), 0.001f);

    vec3 spot_dir(0.0f, -1.0f, 0.0f);

    glUniform3fv(glGetUniformLocation(shaderProgram1, "spot_direction"), 1, spot_dir);

    glUniform4fv(glGetUniformLocation(shaderProgram1, "FogColor"), 1, fog.color);
    glUniform1f(glGetUniformLocation(shaderProgram1, "FogStarting"), fog.FogStarting);
    glUniform1f(glGetUniformLocation(shaderProgram1, "FogEnding"), fog.FogEnding);
    glUniform1f(glGetUniformLocation(shaderProgram1, "ExponentialFogDensity"), fog.ExponentialFogDensity);
    glUniform1i(glGetUniformLocation(shaderProgram1, "FogType"), FogType);

    glUniformMatrix4fv(ModelView, 1, GL_TRUE, ModelViewMatrix);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    vec4 ground_ambient(0.2, 0.2, 0.2, 1.0);
    vec4 ground_diffuse(0.0, 1.0, 0.0, 1.0);
    vec4 ground_specular(0.0, 0.0, 0.0, 1.0);

    float ground_shininess = 5.0;

    vec4 ambient_product = global_ambient * ground_ambient;
    vec4 diffuse_product = light_diffuse * ground_diffuse;
    vec4 specular_product = light_specular * ground_specular;

    glUniform4fv(glGetUniformLocation(shaderProgram1, "ambient_product"), 1, ambient_product);
    glUniform4fv(glGetUniformLocation(shaderProgram1, "diffuse_product"), 1, diffuse_product);
    glUniform4fv(glGetUniformLocation(shaderProgram1, "specular_product"), 1, specular_product);
    glUniform1f(glGetUniformLocation(shaderProgram1, "shininess"), ground_shininess);

    glUniform1i(glGetUniformLocation(shaderProgram1, "lighting_enabled"), 1);
    glUniform1i(glGetUniformLocation(shaderProgram1, "texture_enabled"), EnableGroundTexture ? 1 : 0);

    GroundPlane.draw(shaderProgram1);

    glUniform1i(glGetUniformLocation(shaderProgram1, "texture_enabled"), 0);
    glDepthMask(GL_TRUE);
    glUniform1i(glGetUniformLocation(shaderProgram1, "lighting_enabled"), 0);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram1, "model_view"), 1, GL_TRUE, ModelViewMatrix);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glUniform1i(glGetUniformLocation(shaderProgram1, "lighting_enabled"), 0);
    glUniform4fv(glGetUniformLocation(shaderProgram1, "override_color"), 1, vec4(0.0, 0.0, 0.0, 0.0));

    xAxisLine.draw(shaderProgram1);
    yAxisLine.draw(shaderProgram1);
    zAxisLine.draw(shaderProgram1);

    if (!ExecuteRolling) {
        ModelViewMatrix = ModelViewMatrix * SphereTranslationMatrix * SphereRotationMatrix;

        glUniformMatrix4fv(ModelView, 1, GL_TRUE, ModelViewMatrix);
    }

    bool eyeBelowGround = (eye.y < 0.0f);

    bool lightingForSphere = DrawSolidSphere;
    glUniform1i(glGetUniformLocation(shaderProgram1, "lighting_enabled"), lightingForSphere ? 1 : 0);
    if (DrawSolidSphere)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    else
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    vec4 sphere_material_ambient(0.2, 0.2, 0.2, 1.0);
    vec4 sphere_material_diffuse(1.0, 0.84, 0.0, 1.0);
    vec4 sphere_material_specular(1.0, 0.84, 0.0, 1.0);
    float material_shininess = 125.0;

    vec4 ambient_product2 = global_ambient * sphere_material_ambient;
    vec4 diffuse_product2 = light_diffuse * sphere_material_diffuse;
    vec4 specular_product2 = light_specular * sphere_material_specular;

    glUniform4fv(glGetUniformLocation(shaderProgram1, "ambient_product"), 1, ambient_product2);
    glUniform4fv(glGetUniformLocation(shaderProgram1, "diffuse_product"), 1, diffuse_product2);
    glUniform4fv(glGetUniformLocation(shaderProgram1, "specular_product"), 1, specular_product2);
    glUniform1f(glGetUniformLocation(shaderProgram1, "shininess"), material_shininess);

    glUniform1i(glGetUniformLocation(shaderProgram1, "SphereTextureMode"), 0);

    if (EnableSphereTexture > 0) {
        glUniform1i(glGetUniformLocation(shaderProgram1, "texture_enabled"),
                    RenderTargetObject.texture_enabled ? 1 : 0);
        glUniform1i(glGetUniformLocation(shaderProgram1, "SphereTextureMode"), EnableSphereTexture);
        glUniform1i(glGetUniformLocation(shaderProgram1, "texture_mapping_1D"), 1);
        glUniform1i(glGetUniformLocation(shaderProgram1, "SphereTextureUseVerticalMapping"), VerticalStripes);
        glUniform1i(glGetUniformLocation(shaderProgram1, "SphereTextureUseObjectSpace"), object_space_local_frame);
    } else {
        glUniform1i(glGetUniformLocation(shaderProgram1, "texture_enabled"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram1, "SphereTextureMode"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram1, "texture_mapping_1D"), 1);
        glUniform1i(glGetUniformLocation(shaderProgram1, "SphereTextureUseVerticalMapping"), 1);
        glUniform1i(glGetUniformLocation(shaderProgram1, "SphereTextureUseObjectSpace"), 1);
    }

    if (ToggleLatticeEffect > 0) {
        glUniform1i(glGetUniformLocation(shaderProgram1, "ToggleLatticeEffect"), ToggleLatticeEffect);
        glUniform1i(glGetUniformLocation(shaderProgram1, "UprightLattice"), UprightLattice);
        glUniform1i(glGetUniformLocation(shaderProgram1, "SphereTextureUseObjectSpace"), object_space_local_frame);
    }

    RenderTargetObject.draw(shaderProgram1);

    glUniform1i(glGetUniformLocation(shaderProgram1, "texture_enabled"), 0);
    glUniform1i(glGetUniformLocation(shaderProgram1, "SphereTextureMode"), 0);
    glUniform1i(glGetUniformLocation(shaderProgram1, "ToggleLatticeEffect"), 0);

    if (!eyeBelowGround) {
        if (ShowShadow) {
            glUniform1i(glGetUniformLocation(shaderProgram1, "lighting_enabled"), 1);

            if (EnableBlendingShadows) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            } else {
                glDisable(GL_BLEND);
            }

            glDepthMask(GL_TRUE);
            glUniform1i(glGetUniformLocation(shaderProgram1, "lighting_enabled"), 0);
            glUniform4fv(glGetUniformLocation(shaderProgram1, "override_color"), 1, shadow_color);

            ModelViewMatrix = LookAt(eye, at, up) * shadow_proj_matrix * SphereTranslationMatrix;

            if (ExecuteRolling) {
                ModelViewMatrix *= Rotate(CurrentRotationAngle, SphereAxisForRotation.x, SphereAxisForRotation.y,
                                          SphereAxisForRotation.z);
            } else {
                ModelViewMatrix *= SphereRotationMatrix;
            }

            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -1.0f);
            glUniformMatrix4fv(ModelView, 1, GL_TRUE, ModelViewMatrix);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);

            if (ToggleLatticeEffect > 0) {
                glUniform1i(glGetUniformLocation(shaderProgram1, "ToggleLatticeEffect"), ToggleLatticeEffect);
                glUniform1i(glGetUniformLocation(shaderProgram1, "UprightLattice"), UprightLattice);
                glUniform1i(glGetUniformLocation(shaderProgram1, "SphereTextureUseObjectSpace"),
                            object_space_local_frame);
            }

            RenderTargetObject.draw(shaderProgram1);

            glUniform1i(glGetUniformLocation(shaderProgram1, "ToggleLatticeEffect"), 0);
            glDisable(GL_CULL_FACE);
            glDisable(GL_POLYGON_OFFSET_FILL);

            ModelViewMatrix = LookAt(eye, at, up);

            glUniformMatrix4fv(ModelView, 1, GL_TRUE, ModelViewMatrix);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            GroundPlane.draw(shaderProgram1);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glUniform1i(glGetUniformLocation(shaderProgram1, "lighting_enabled"), 1);
            glUniform4fv(glGetUniformLocation(shaderProgram1, "override_color"), 1, vec4(0, 0, 0, 0));
        }
    }

    glutSwapBuffers();
    glutPostRedisplay();
}

void idle() {
    if (RollingStarted) {
        if (!RollingPaused)
            SmoothRollingMode();
    }

    glutPostRedisplay();
}

void keyboard(const unsigned char key, int x, int y) {
    switch (key) {

        // 'x' and the 'X' keys respectively decrease and increase the viewer x-coordinate by 1.0
        case 'x':
            eye.x -= 1.0f;
            break;
        case 'X':
            eye.x += 1.0f;
            break;

        // 'y' and the 'Y' keys respectively decrease and increase the viewer y-coordinate by 1.0
        case 'y':
            eye.y -= 1.0f;
            break;
        case 'Y':
            eye.y += 1.0f;
            break;

        // 'z' and the 'Z' keys respectively decrease and increase the viewer z-coordinate by 1.0
        case 'z':
            eye.z -= 1.0f;
            break;
        case 'Z':
            eye.z += 1.0f;
            break;

        case 'v':
        case 'V':
            VerticalStripes = 1; // Vertical stripes
            break;

        case 's':
        case 'S':
            VerticalStripes = 0; // Slanted stripes
            break;

        case 'o':
        case 'O':
            object_space_local_frame = 1; // Use object space (local frame)
            break;

        case 'e':
        case 'E':
            object_space_local_frame = 0; // Use eye space (camera frame)
            break;

        case 'l':
        case 'L': {
            ToggleLatticeEffect = (ToggleLatticeEffect == 0) ? 1 : 0; // Turns the lattice effect on or off
            UprightLattice = 1; // Switches the lattice effect mode to “upright”
        }
        break;

        case 'u':
        case 'U':
            UprightLattice = 1; // Switches the lattice effect mode to “upright”
            break;

        case 't':
        case 'T':
            UprightLattice = 0; // Switches the lattice effect mode to “tilted”
            break;

        default:
            break;
    }

    glutPostRedisplay();
}

void mouse(const int button, const int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        if (!RollingStarted)
            RollingStarted = true;
        else
            RollingPaused = !RollingPaused;
    }

}

void menu(const int MenuSelection) {
    switch (MenuSelection) {
        case 1:
            eye = InitialCameraPosition;
            break;
        case 2:
            exit(EXIT_SUCCESS);
        case 3:
            DrawSolidSphere = false;
            break;

        case 4:
            DrawSolidSphere = true;
            UseSmoothShading = false;
            RenderTargetObject.ComputeNormals(UseSmoothShading);
            RenderTargetObject.InitializeOpenGLBufferData();
            break;
        case 5:
            DrawSolidSphere = true;
            UseSmoothShading = true;
            RenderTargetObject.ComputeNormals(UseSmoothShading);
            RenderTargetObject.InitializeOpenGLBufferData();
            break;

        case 6:
            FogType = 0;
            break;
        case 7:
            FogType = 1;
            break;
        case 8:
            FogType = 2;
            break;
        case 9:
            FogType = 3;
            break;

        case 10:
            ShowShadow = false;
            break;
        case 11:
            ShowShadow = true;
            EnableBlendingShadows = 1;
            break;
        case 12:
            ShowShadow = true;
            EnableBlendingShadows = 0;
            break;

        case 13:
            EnableGroundTexture = false;
            break;
        case 14:
            EnableBlendingShadows = 1;
            EnableGroundTexture = true;
            break;

        case 15:
            EnableSphereTexture = 0;
            break;
        case 16:
            EnableSphereTexture = 1;
            break;
        case 17:
            EnableSphereTexture = 2;
            break;

        case 18:
            EnableFireworks = 0;
            break;
        case 19:
            if (EnableFireworks == 0) {
                fireworks.FireworkCycleStartTime = static_cast<float>(glutGet(GLUT_ELAPSED_TIME));
            }
            EnableFireworks = 1;
            break;
        default:
            break;
    }

    glutPostRedisplay();
}

void reshape(const int Width, const int Height) {
    glViewport(0, 0, Width, Height);
    aspect = static_cast<GLfloat>(Width) / static_cast<GLfloat>(Height);
    glutPostRedisplay();
}

int main(int argc, char **argv) {
    glutInit(&argc, argv);

#ifdef __APPLE__
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH | GLUT_3_2_CORE_PROFILE);
#else
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
#endif
    glutInitWindowSize(1024, 1024);
    glutCreateWindow("Sphere Drift");
#ifdef __APPLE__
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
#else
    int err = glewInit();
    if (GLEW_OK != err) {
        printf("Error: glewInit failed: %s\n", (char*) glewGetErrorString(err));
        exit(1);
    }
#endif

    printf("Renderer: %s\n", reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    printf("OpenGL version supported %s\n", reinterpret_cast<const char *>(glGetString(GL_VERSION)));

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);
    glutKeyboardFunc(keyboard);

    glutMouseFunc(mouse);

    const int shading_submenu = glutCreateMenu(menu);
    glutAddMenuEntry("Flat Shading", 4);
    glutAddMenuEntry("Smooth Shading", 5);

    const int fog_submenu = glutCreateMenu(menu);
    glutAddMenuEntry("No Fog", 6);
    glutAddMenuEntry("Linear", 7);
    glutAddMenuEntry("Exponential", 8);
    glutAddMenuEntry("Exponential Square", 9);

    const int shadow_submenu = glutCreateMenu(menu);
    glutAddMenuEntry("No", 10);
    glutAddMenuEntry("Yes - Blended", 11);
    glutAddMenuEntry("Yes - Solid", 12);

    const int ground_texture_submenu = glutCreateMenu(menu);
    glutAddMenuEntry("No", 13);
    glutAddMenuEntry("Yes", 14);

    const int sphere_texture_submenu = glutCreateMenu(menu);
    glutAddMenuEntry("No", 15);
    glutAddMenuEntry("Yes - Contour Lines", 16);
    glutAddMenuEntry("Yes - Checkerboard", 17);

    const int fireworks_submenu = glutCreateMenu(menu);
    glutAddMenuEntry("No", 18);
    glutAddMenuEntry("Yes", 19);

    glutCreateMenu(menu);
    glutAddMenuEntry("Default View Point", 1);
    glutAddMenuEntry("Quit", 2);
    glutAddMenuEntry("Wire Frame Sphere", 3);
    glutAddSubMenu("Shading", shading_submenu);
    glutAddSubMenu("Fog Options", fog_submenu);
    glutAddSubMenu("Shadow", shadow_submenu);
    glutAddSubMenu("Texture Mapped Ground", ground_texture_submenu);
    glutAddSubMenu("Texture Mapped Sphere", sphere_texture_submenu);
    glutAddSubMenu("Fireworks", fireworks_submenu);

    glutAttachMenu(GLUT_RIGHT_BUTTON);

    init();
    glutMainLoop();
    return 0;
}