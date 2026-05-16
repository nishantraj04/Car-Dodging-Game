#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr unsigned WindowWidth = 520;
constexpr unsigned WindowHeight = 760;
constexpr float RoadLeft = 44.f;
constexpr float RoadWidth = 432.f;
constexpr float RoadRight = RoadLeft + RoadWidth;
constexpr int LaneCount = 4;
constexpr float LaneWidth = RoadWidth / LaneCount;

float laneCenter(int lane) {
    return RoadLeft + LaneWidth * lane + LaneWidth * 0.5f;
}

std::string assetPath(const std::string& file) {
    return "Assets/" + file;
}

bool loadOptionalSound(sf::SoundBuffer& buffer, const std::string& file) {
    return buffer.loadFromFile(assetPath(file));
}

struct Difficulty {
    std::string name;
    float baseSpeed;
    float spawnInterval;
    float speedRamp;
    int simultaneousCars;
};

const std::array<Difficulty, 3> Difficulties{{
    {"Easy", 220.f, 1.00f, 10.f, 5},
    {"Medium", 280.f, 0.72f, 16.f, 6},
    {"Hard", 340.f, 0.52f, 24.f, 7},
}};
} // namespace

class Car {
public:
    virtual ~Car() = default;

    sf::FloatRect bounds() const {
        auto box = sprite_.getGlobalBounds();
        const float insetX = box.width * 0.18f;
        const float insetY = box.height * 0.12f;
        return {box.left + insetX, box.top + insetY, box.width - insetX * 2.f, box.height - insetY * 2.f};
    }

    void draw(sf::RenderWindow& window) const {
        window.draw(sprite_);
    }

protected:
    sf::Sprite sprite_;
};

class PlayerCar : public Car {
public:
    void setup(const sf::Texture& texture) {
        sprite_.setTexture(texture);
        const float targetWidth = 68.f;
        const float scale = targetWidth / static_cast<float>(texture.getSize().x);
        sprite_.setScale(scale, scale);
        currentLane_ = 1;
        targetX_ = laneCenter(currentLane_);
        sprite_.setOrigin(texture.getSize().x * 0.5f, texture.getSize().y * 0.5f);
        targetY_ = WindowHeight - 100.f;
        sprite_.setPosition(targetX_, targetY_);
    }

    void update(float dt) {
        const float currentX = sprite_.getPosition().x;
        const float currentY = sprite_.getPosition().y;
        const float deltaX = targetX_ - currentX;
        const float deltaY = targetY_ - currentY;
        const float step = moveSpeed_ * dt;
        float nextX = currentX;
        float nextY = currentY;

        if (std::abs(deltaX) <= step) {
            nextX = targetX_;
        } else {
            nextX += (deltaX > 0.f ? step : -step);
        }

        if (std::abs(deltaY) <= step) {
            nextY = targetY_;
        } else {
            nextY += (deltaY > 0.f ? step : -step);
        }

        sprite_.setPosition(nextX, nextY);
    }

    bool moveLeft() {
        if (currentLane_ <= 0) {
            return false;
        }
        --currentLane_;
        targetX_ = laneCenter(currentLane_);
        return true;
    }

    bool moveRight() {
        if (currentLane_ >= LaneCount - 1) {
            return false;
        }
        ++currentLane_;
        targetX_ = laneCenter(currentLane_);
        return true;
    }

    bool moveUp() {
        if (targetY_ <= minY_) {
            return false;
        }
        targetY_ = std::max(minY_, targetY_ - verticalStep_);
        return true;
    }

    bool moveDown() {
        if (targetY_ >= maxY_) {
            return false;
        }
        targetY_ = std::min(maxY_, targetY_ + verticalStep_);
        return true;
    }

    int lane() const {
        return currentLane_;
    }

private:
    int currentLane_ = 1;
    float targetX_ = 0.f;
    float targetY_ = 0.f;
    float moveSpeed_ = 720.f;
    float verticalStep_ = 54.f;
    float minY_ = 105.f;
    float maxY_ = WindowHeight - 80.f;
};

class EnemyCar : public Car {
public:
    EnemyCar(const sf::Texture& texture, int lane, float y, float speed) : speed_(speed), lane_(lane) {
        sprite_.setTexture(texture);
        const float targetWidth = 68.f;
        const float scale = targetWidth / static_cast<float>(texture.getSize().x);
        sprite_.setScale(scale, scale);
        sprite_.setOrigin(texture.getSize().x * 0.5f, texture.getSize().y * 0.5f);
        sprite_.setRotation(180.f);
        sprite_.setPosition(laneCenter(lane), y);
    }

    void update(float dt) {
        sprite_.move(0.f, speed_ * dt);
    }

    bool isOffScreen() const {
        return sprite_.getPosition().y - sprite_.getGlobalBounds().height > WindowHeight + 40.f;
    }

    int lane() const {
        return lane_;
    }

private:
    float speed_ = 0.f;
    int lane_ = 0;
};

class Game {
public:
    Game()
        : window_(sf::VideoMode(WindowWidth, WindowHeight), "Car Dodging Game", sf::Style::Titlebar | sf::Style::Close),
          rng_(static_cast<unsigned>(std::time(nullptr))) {
        window_.setFramerateLimit(60);
        loadAssets();
        setupRoad();
        player_.setup(playerTexture_);
        restart();
    }

    void run() {
        sf::Clock clock;
        while (window_.isOpen()) {
            const float dt = clock.restart().asSeconds();
            handleEvents();
            if (!paused_ && !gameOver_) {
                update(dt);
            }
            render();
        }
    }

private:
    void loadAssets() {
        if (!playerTexture_.loadFromFile(assetPath("WhiteCar.png"))) {
            throw std::runtime_error("Could not load Assets/WhiteCar.png");
        }

        const std::array<std::string, 5> enemyFiles{
            "RedCar1.png", "RedCar2.png", "YellowCar1.png", "YellowCar2.png", "YellowCar3.png"};
        for (const auto& file : enemyFiles) {
            sf::Texture texture;
            if (!texture.loadFromFile(assetPath(file))) {
                throw std::runtime_error("Could not load " + assetPath(file));
            }
            enemyTextures_.push_back(std::move(texture));
        }

        fontLoaded_ = font_.loadFromFile("C:/Windows/Fonts/arial.ttf") ||
                      font_.loadFromFile("C:/Windows/Fonts/calibri.ttf");

        if (music_.openFromFile(assetPath("background.ogg"))) {
            music_.setLoop(true);
            music_.setVolume(35.f);
            music_.play();
        }

        if (loadOptionalSound(collisionBuffer_, "collision.wav")) {
            collisionSound_.setBuffer(collisionBuffer_);
            collisionSound_.setVolume(80.f);
        }
        if (loadOptionalSound(moveBuffer_, "move.wav")) {
            moveSound_.setBuffer(moveBuffer_);
            moveSound_.setVolume(35.f);
        }
    }

    void setupRoad() {
        road_.setSize({RoadWidth, static_cast<float>(WindowHeight)});
        road_.setPosition(RoadLeft, 0.f);
        road_.setFillColor(sf::Color(3, 4, 4));

        leftGrass_.setSize({RoadLeft, static_cast<float>(WindowHeight)});
        leftGrass_.setFillColor(sf::Color::White);

        rightGrass_.setSize({WindowWidth - RoadRight, static_cast<float>(WindowHeight)});
        rightGrass_.setPosition(RoadRight, 0.f);
        rightGrass_.setFillColor(sf::Color::White);
    }

    void restart() {
        enemies_.clear();
        elapsed_ = 0.f;
        score_ = 0;
        dodged_ = 0;
        nextSpawn_ = 0.45f;
        gameOver_ = false;
        paused_ = false;
        player_.setup(playerTexture_);
        seedStartingTraffic();
    }

    void handleEvents() {
        sf::Event event{};
        while (window_.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                window_.close();
            }
            if (event.type != sf::Event::KeyPressed) {
                continue;
            }

            const auto key = event.key.code;
            if (key == sf::Keyboard::Escape) {
                window_.close();
            } else if (key == sf::Keyboard::P && !gameOver_) {
                paused_ = !paused_;
            } else if (key == sf::Keyboard::R && gameOver_) {
                restart();
            } else if (key == sf::Keyboard::Num1) {
                difficultyIndex_ = 0;
                restart();
            } else if (key == sf::Keyboard::Num2) {
                difficultyIndex_ = 1;
                restart();
            } else if (key == sf::Keyboard::Num3) {
                difficultyIndex_ = 2;
                restart();
            } else if (!paused_ && !gameOver_) {
                bool moved = false;
                if (key == sf::Keyboard::Left || key == sf::Keyboard::A) {
                    moved = player_.moveLeft();
                } else if (key == sf::Keyboard::Right || key == sf::Keyboard::D) {
                    moved = player_.moveRight();
                } else if (key == sf::Keyboard::Up || key == sf::Keyboard::W) {
                    moved = player_.moveUp();
                } else if (key == sf::Keyboard::Down || key == sf::Keyboard::S) {
                    moved = player_.moveDown();
                }
                if (moved && moveSound_.getBuffer()) {
                    moveSound_.play();
                }
            }
        }
    }

    void update(float dt) {
        elapsed_ += dt;
        score_ = static_cast<int>(elapsed_ * 10.f) + dodged_ * 25;
        roadOffset_ = std::fmod(roadOffset_ + currentSpeed() * dt, 96.f);

        player_.update(dt);

        nextSpawn_ -= dt;
        if (nextSpawn_ <= 0.f && static_cast<int>(enemies_.size()) < difficulty().simultaneousCars) {
            spawnEnemy();
            nextSpawn_ = nextInterval();
        }

        for (auto& enemy : enemies_) {
            enemy.update(dt);
        }

        const auto before = enemies_.size();
        enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), [](const EnemyCar& enemy) {
                           return enemy.isOffScreen();
                       }),
                       enemies_.end());
        dodged_ += static_cast<int>(before - enemies_.size());

        for (const auto& enemy : enemies_) {
            if (player_.bounds().intersects(enemy.bounds())) {
                gameOver_ = true;
                if (collisionSound_.getBuffer()) {
                    collisionSound_.play();
                }
                break;
            }
        }
    }

    const Difficulty& difficulty() const {
        return Difficulties[difficultyIndex_];
    }

    float currentSpeed() const {
        return difficulty().baseSpeed + elapsed_ * difficulty().speedRamp;
    }

    int speedLevel() const {
        return difficultyIndex_ + 1 + static_cast<int>(elapsed_ / 20.f);
    }

    int level() const {
        return 1 + static_cast<int>(elapsed_ / 15.f);
    }

    float nextInterval() {
        std::uniform_real_distribution<float> variation(0.72f, 1.2f);
        const float pressure = std::min(0.45f, elapsed_ / 95.f);
        return std::max(0.26f, difficulty().spawnInterval * variation(rng_) - pressure);
    }

    void spawnEnemy() {
        std::uniform_int_distribution<int> laneDist(0, LaneCount - 1);
        std::uniform_int_distribution<int> textureDist(0, static_cast<int>(enemyTextures_.size() - 1));
        std::uniform_real_distribution<float> speedDist(0.f, 55.f);

        int lane = laneDist(rng_);
        for (int attempts = 0; attempts < 8 && isLaneBlocked(lane); ++attempts) {
            lane = laneDist(rng_);
        }

        const float spawnY = hasOpeningAhead() ? -125.f : -250.f;
        enemies_.emplace_back(enemyTextures_[textureDist(rng_)], lane, spawnY, currentSpeed() + speedDist(rng_));
    }

    void seedStartingTraffic() {
        const std::array<int, 4> lanes{0, 2, 3, 0};
        const std::array<float, 4> yPositions{150.f, 310.f, 470.f, 640.f};
        for (std::size_t index = 0; index < lanes.size(); ++index) {
            enemies_.emplace_back(enemyTextures_[index], lanes[index], yPositions[index], currentSpeed() * 0.55f);
        }
    }

    bool isLaneBlocked(int lane) const {
        return std::any_of(enemies_.begin(), enemies_.end(), [lane](const EnemyCar& enemy) {
            const auto top = enemy.bounds().top;
            return enemy.lane() == lane && top < 230.f;
        });
    }

    bool hasOpeningAhead() const {
        const int nearTopCars = static_cast<int>(std::count_if(enemies_.begin(), enemies_.end(), [](const EnemyCar& enemy) {
            const auto box = enemy.bounds();
            return box.top < 260.f && box.top + box.height > -120.f;
        }));
        return nearTopCars < 2;
    }

    void render() {
        window_.clear(sf::Color::White);
        drawRoad();

        for (const auto& enemy : enemies_) {
            enemy.draw(window_);
        }
        player_.draw(window_);

        drawHud();
        if (paused_) {
            drawCenterMessage("PAUSED", "Press P to resume");
        }
        if (gameOver_) {
            drawCenterMessage("GAME OVER", "Press R to restart");
        }

        window_.display();
    }

    void drawRoad() {
        window_.draw(leftGrass_);
        window_.draw(rightGrass_);
        window_.draw(road_);

        sf::RectangleShape rightShoulder({RoadWidth * 0.28f, static_cast<float>(WindowHeight)});
        rightShoulder.setPosition(RoadLeft + RoadWidth * 0.72f, 0.f);
        rightShoulder.setFillColor(sf::Color(13, 16, 16));
        window_.draw(rightShoulder);

        sf::RectangleShape border({3.f, static_cast<float>(WindowHeight)});
        border.setFillColor(sf::Color(80, 80, 80));
        border.setPosition(RoadLeft, 0.f);
        window_.draw(border);
        border.setPosition(RoadRight - 3.f, 0.f);
        window_.draw(border);

        sf::RectangleShape dash({16.f, 60.f});
        dash.setFillColor(sf::Color::White);
        const float centerX = RoadLeft + RoadWidth * 0.5f - 8.f;
        for (float y = -96.f + roadOffset_; y < WindowHeight + 96.f; y += 82.f) {
            dash.setPosition(centerX, y);
            window_.draw(dash);
        }
    }

    void drawHud() {
        if (!fontLoaded_) {
            return;
        }

        sf::Text scoreText;
        scoreText.setFont(font_);
        scoreText.setCharacterSize(20);
        scoreText.setStyle(sf::Text::Bold | sf::Text::Italic);
        scoreText.setFillColor(sf::Color::White);
        scoreText.setString("SCORE: " + std::to_string(score_));
        scoreText.setPosition(RoadLeft + 54.f, 22.f);
        window_.draw(scoreText);

        sf::Text levelText;
        levelText.setFont(font_);
        levelText.setCharacterSize(20);
        levelText.setStyle(sf::Text::Bold | sf::Text::Italic);
        levelText.setFillColor(sf::Color::White);
        levelText.setString("LEVEL: " + std::to_string(level()));
        levelText.setPosition(RoadLeft + RoadWidth * 0.5f - 56.f, 22.f);
        window_.draw(levelText);

        sf::Text speedText;
        speedText.setFont(font_);
        speedText.setCharacterSize(20);
        speedText.setStyle(sf::Text::Bold | sf::Text::Italic);
        speedText.setFillColor(sf::Color::White);
        speedText.setString("SPEED: " + std::to_string(speedLevel()));
        speedText.setPosition(RoadLeft + RoadWidth - 136.f, 22.f);
        window_.draw(speedText);
    }

    void drawCenterMessage(const std::string& title, const std::string& subtitle) {
        if (!fontLoaded_) {
            return;
        }

        sf::RectangleShape overlay({WindowWidth, WindowHeight});
        overlay.setFillColor(sf::Color(0, 0, 0, 135));
        window_.draw(overlay);

        sf::Text titleText(title, font_, 46);
        titleText.setStyle(sf::Text::Bold);
        titleText.setFillColor(sf::Color::White);
        titleText.setOrigin(titleText.getLocalBounds().width * 0.5f, titleText.getLocalBounds().height * 0.5f);
        titleText.setPosition(WindowWidth * 0.5f, WindowHeight * 0.46f);
        window_.draw(titleText);

        sf::Text subText(subtitle, font_, 22);
        subText.setFillColor(sf::Color(245, 225, 100));
        subText.setOrigin(subText.getLocalBounds().width * 0.5f, subText.getLocalBounds().height * 0.5f);
        subText.setPosition(WindowWidth * 0.5f, WindowHeight * 0.54f);
        window_.draw(subText);
    }

    sf::RenderWindow window_;
    sf::Texture playerTexture_;
    std::vector<sf::Texture> enemyTextures_;
    sf::Font font_;
    bool fontLoaded_ = false;

    sf::Music music_;
    sf::SoundBuffer collisionBuffer_;
    sf::SoundBuffer moveBuffer_;
    sf::Sound collisionSound_;
    sf::Sound moveSound_;

    sf::RectangleShape road_;
    sf::RectangleShape leftGrass_;
    sf::RectangleShape rightGrass_;
    PlayerCar player_;
    std::vector<EnemyCar> enemies_;
    std::mt19937 rng_;

    std::size_t difficultyIndex_ = 0;
    float elapsed_ = 0.f;
    float nextSpawn_ = 0.f;
    float roadOffset_ = 0.f;
    int score_ = 0;
    int dodged_ = 0;
    bool paused_ = false;
    bool gameOver_ = false;
};

int main() {
    try {
        Game game;
        game.run();
    } catch (const std::exception& error) {
        sf::RenderWindow window(sf::VideoMode(640, 180), "Car Dodging Game - Error");
        sf::Font font;
        font.loadFromFile("C:/Windows/Fonts/arial.ttf");
        sf::Text message(error.what(), font, 18);
        message.setFillColor(sf::Color::White);
        message.setPosition(20.f, 70.f);
        while (window.isOpen()) {
            sf::Event event{};
            while (window.pollEvent(event)) {
                if (event.type == sf::Event::Closed || event.type == sf::Event::KeyPressed) {
                    window.close();
                }
            }
            window.clear(sf::Color(80, 20, 20));
            window.draw(message);
            window.display();
        }
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
