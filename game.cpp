#include <ncurses.h>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <atomic>
#include <condition_variable>

std::mutex mtx;        // Mutex para sincronização geral
bool running = true;   // Controle do loop principal
bool gameOver = false; // Estado do jogo (ativo ou encerrado)

// Mutex e variáveis de condição para o depósito
std::mutex depotMutex;
std::condition_variable depotNotFull;
std::condition_variable depotNotEmpty;
bool truckUnloading = false;
bool helicopterReloading = false;

// Estrutura para o dinossauro
struct Dino
{
    int x, y;         // Posição do dinossauro
    bool alive;       // Estado do dinossauro (vivo ou morto)
    bool movingRight; // Direção do movimento
};

// Estrutura para os mísseis
struct Missile
{
    int x, y;         // Posição do míssil
    bool active;      // Indica se o míssil ainda está ativo
    bool movingRight; // Direção do míssil (direita ou esquerda)
};

std::vector<std::thread> missileThreads; // Armazenar threads dos mísseis
std::vector<Dino> dinos;                 // Lista de dinossauros

// Representação do dinossauro
const char *dinoForm[6] = {
    "              __",
    "             / _)",
    "    _/\\/\\/\\_/ /",
    "   _|         /",
    "  _|  (  | (  |",
    " /__.-|_|--|_|"};

const char *dinoReversed[6] = {
    " __              ",
    "(_ \\             ",
    " \\_ \\_/\\/\\/\\/    ",
    "  \\         |_   ",
    "  | )  | )  |_   ",
    " |_|--|_|-.__\\ "};

// Representação do caminhão
const char *truckRight[6] = {
    "     __             ",
    "   _|__| ___      ",
    " _||_|_||___\\___  ",
    "|   _   |~ _  '-.",
    "'--(_)----(_)--'"};

const char *truckLeft[5] = {
    "      ___ ",
    "  ___/___|______",
    ".-' _  ~ | _    |",
    " -(_)----(_)---'"};

int truckWidth = 30; // Largura do caminhão

// Variáveis para o helicóptero
int helicopterX = 40, helicopterY = 10; // Posição inicial do helicóptero
bool helicopterMovingRight = true;      // Direção inicial do helicóptero
int width = 100, height = 40;

// Variáveis para os mísseis do helicóptero e do depósito
const int MAX_HELICOPTER_MISSILES = 10;
std::atomic<int> helicopterMissiles{MAX_HELICOPTER_MISSILES};

const int MAX_DEPOT_MISSILES = 10;
std::atomic<int> depotMissiles{MAX_DEPOT_MISSILES};

int depositX = 0, depositY = 0; // Coordenadas do depósito

enum class HelicopterState
{
    Normal,
    Reloading
};
HelicopterState helicopterState = HelicopterState::Normal;
std::chrono::time_point<std::chrono::steady_clock> reloadStartTime;
const auto HELICOPTER_RELOAD_TIME = std::chrono::seconds(1);

void drawSkyAndGrass(int width, int height)
{
    for (int y = 0; y < height; y++)
    {
        attron(COLOR_PAIR(y < height / 3 ? 1 : 2)); // Céu ou grama
        for (int x = 0; x < width; x++)
        {
            mvprintw(y, x, " ");
        }
    }
    refresh();
}

void drawDino(const Dino &dino)
{
    const char **form = dino.movingRight ? dinoForm : dinoReversed;
    for (int i = 0; i < 6; i++)
    {
        mvaddstr(dino.y + i, dino.x, form[i]);
    }
    refresh();
}

void eraseDino(const Dino &dino)
{
    for (int i = 0; i < 6; i++)
    {
        mvaddstr(dino.y + i, dino.x, "                    ");
    }
    refresh();
}

void drawHelicopter(int x, int y, bool movingRight)
{
    if (movingRight)
    {
        mvprintw(y, x, "   __|__ ");
        mvprintw(y + 1, x, "--@--@--o");
    }
    else
    {
        mvprintw(y, x, " __|__   ");
        mvprintw(y + 1, x, "o--@--@--");
    }
    refresh();
}

void eraseHelicopter(int x, int y)
{
    mvprintw(y, x, "         ");
    mvprintw(y + 1, x, "         ");
    refresh();
}

void drawMissile(Missile &missile)
{
    if (missile.active && missile.x < width && missile.x >= 0)
    {
        mvprintw(missile.y, missile.x, "-");
    }
    refresh();
}

void eraseMissile(Missile &missile)
{
    mvprintw(missile.y, missile.x, " ");
    refresh();
}

void drawTruck(int x, int y, bool movingRight)
{
    const char **truckForm = movingRight ? truckRight : truckLeft;
    for (int i = 0; i < 5; i++)
    {
        if (x >= 0 && x < width)
            mvaddstr(y + i, x, truckForm[i]);
    }
    refresh();
}

void eraseTruck(int x, int y)
{
    for (int i = 0; i < 5; i++)
    {
        mvaddstr(y + i, x, std::string(truckWidth, ' ').c_str());
    }
    refresh();
}

void drawDeposit(int x, int y)
{
    mvprintw(y, x, "      _______");
    mvprintw(y + 1, x, "     /       \\");
    mvprintw(y + 2, x, "    /_________\\");
    mvprintw(y + 3, x, "    |         |");
    mvprintw(y + 4, x, "    |         |");
    mvprintw(y + 5, x, "    |_________|");
    refresh();
}

bool isHelicopterAtDepot()
{
    // Dimensões do helicóptero
    int helicopterWidth = 9;
    int helicopterHeight = 2;

    // Dimensões do depósito
    int depotWidth = 15;
    int depotHeight = 6;

    // Verificar sobreposição
    return (helicopterX + helicopterWidth >= depositX &&
            helicopterX <= depositX + depotWidth &&
            helicopterY + helicopterHeight >= depositY &&
            helicopterY <= depositY + depotHeight);
}

void unloadMissilesToDepot(int amount)
{
    std::unique_lock<std::mutex> lock(depotMutex);
    // Espera até que haja espaço no depósito e o helicóptero não esteja recarregando
    while ((depotMissiles >= MAX_DEPOT_MISSILES) || helicopterReloading)
    {
        depotNotFull.wait(lock);
    }
    // Sinaliza que o caminhão está descarregando
    truckUnloading = true;

    // Descarrega mísseis no depósito
    int spaceAvailable = MAX_DEPOT_MISSILES - depotMissiles;
    int missilesToUnload = std::min(amount, spaceAvailable);
    depotMissiles += missilesToUnload;

    // Simula tempo de descarregamento
    lock.unlock();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    lock.lock();

    // Termina o descarregamento
    truckUnloading = false;
    // Notifica que o depósito não está vazio (para o helicóptero)
    depotNotEmpty.notify_all();
    // Notifica que o depósito pode não estar cheio (para outros caminhões)
    depotNotFull.notify_all();
}


// Modifique a função truckAnimation
void truckAnimation()
{
    int truckX = -truckWidth;
    int truckY = height - 10;
    bool truckMovingRight = true;

    while (running && !gameOver)
    {
        // Caminhão traz mísseis de tempos em tempos
        std::this_thread::sleep_for(std::chrono::seconds(15)); // Tempo entre viagens do caminhão

        // Caminhão entra na tela movendo para a direita
        truckMovingRight = true;
        truckX = -truckWidth;

        while (truckX < depositX - 5 && running && !gameOver)
        {
            {
                std::lock_guard<std::mutex> lock(mtx);
                eraseTruck(truckX - 1, truckY);
                truckX++;
                if (truckX >= 0 && truckX < width)
                    drawTruck(truckX, truckY, truckMovingRight);
                drawDeposit(depositX, depositY);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Chegada ao depósito
        {
            std::lock_guard<std::mutex> lock(mtx);
            mvprintw(3, 0, "Caminhão chegou ao depósito. Tentando reabastecer...          ");
        }

        unloadMissilesToDepot(MAX_DEPOT_MISSILES);

        {
            std::lock_guard<std::mutex> lock(mtx);
            mvprintw(2, 0, "Depósito reabastecido pelo caminhão.                          ");
            mvprintw(3, 0, "                                                              ");
        }

        // Caminhão vira para a esquerda e sai do depósito
        truckMovingRight = false;

        while (truckX > -truckWidth && running && !gameOver)
        {
            {
                std::lock_guard<std::mutex> lock(mtx);
                eraseTruck(truckX + 1, truckY);
                truckX--;
                if (truckX >= 0 && truckX < width)
                    drawTruck(truckX, truckY, truckMovingRight);
                drawDeposit(depositX, depositY);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Limpar qualquer resíduo do caminhão que possa ficar na tela
        {
            std::lock_guard<std::mutex> lock(mtx);
            eraseTruck(truckX + 1, truckY);
        }

        // Esperar antes de iniciar a próxima viagem
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

bool checkCollisionWithDinoHead(const Missile &missile, const Dino &dino)
{
    if (!dino.alive)
        return false;

    // Coordenadas da cabeça do dinossauro
    int headX = dino.x + (dino.movingRight ? 14 : 5);
    int headY = dino.y + 1;

    // Verificar colisão com a cabeça
    return (missile.x == headX && missile.y == headY);
}

bool checkCollisionWithDinoBody(const Missile &missile, const Dino &dino)
{
    if (!dino.alive)
        return false;

    // Dimensões do dinossauro
    int dinoWidth = 20;
    int dinoHeight = 6;

    // Verificar colisão com o corpo
    return (missile.x >= dino.x && missile.x < dino.x + dinoWidth &&
            missile.y >= dino.y + 2 && missile.y < dino.y + dinoHeight);
}

bool checkCollisionWithHelicopter(const Dino &dino)
{
    // Dimensões do helicóptero
    int helicopterWidth = 9;
    int helicopterHeight = 2;

    // Verificar colisão entre o helicóptero e o dinossauro
    return (helicopterX < dino.x + 20 && helicopterX + helicopterWidth > dino.x &&
            helicopterY < dino.y + 6 && helicopterY + helicopterHeight > dino.y);
}

void missileThread(Missile missile)
{
    while (missile.active && running && !gameOver)
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            eraseMissile(missile);

            missile.x += missile.movingRight ? 1 : -1;

            // Verificar colisão com cada dinossauro
            for (auto &dino : dinos)
            {
                if (checkCollisionWithDinoHead(missile, dino))
                {
                    dino.alive = false;
                    eraseDino(dino);
                    missile.active = false;
                    break;
                }
                else if (checkCollisionWithDinoBody(missile, dino))
                {
                    missile.active = false;
                    break;
                }
            }

            if (missile.x < width && missile.x >= 0 && missile.active)
            {
                drawMissile(missile);
            }
            else
            {
                missile.active = false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

int countAliveDinos()
{
    int count = 0;
    for (const auto &dino : dinos)
    {
        if (dino.alive)
            count++;
    }
    return count;
}

void dinoAnimation()
{
    while (running && !gameOver)
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            for (auto &dino : dinos)
            {
                if (dino.alive)
                {
                    eraseDino(dino);

                    if (dino.movingRight)
                    {
                        dino.x++;
                        if (dino.x > width - 20)
                        {
                            dino.movingRight = false;
                        }
                    }
                    else
                    {
                        dino.x--;
                        if (dino.x < 0)
                        {
                            dino.movingRight = true;
                        }
                    }

                    drawDino(dino);

                    // Verificar colisão com o helicóptero
                    if (checkCollisionWithHelicopter(dino))
                    {
                        gameOver = true;
                    }
                }
            }

            // Verificar Game Over
            if (countAliveDinos() >= 5)
            {
                gameOver = true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (gameOver)
    {
        std::lock_guard<std::mutex> lock(mtx);
        mvprintw(height / 2, width / 2 - 5, "GAME OVER");
        refresh();
    }
}

void spawnDino()
{
    while (running && !gameOver)
    {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (countAliveDinos() < 5)
            {
                int randomHeight = height - 8 - (rand() % 5);
                Dino newDino = {0, randomHeight, true, true};
                dinos.push_back(newDino);
            }
        }
    }
}

int main()
{
    initscr();
    start_color();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);

    init_pair(1, COLOR_BLUE, COLOR_BLUE);   // Céu
    init_pair(2, COLOR_GREEN, COLOR_GREEN); // Grama

    srand(time(0));
    drawSkyAndGrass(width, height);

    depositX = width - 20;
    depositY = height - 15;

    std::thread dinoThread(dinoAnimation);
    std::thread spawnThread(spawnDino);
    std::thread truckThread(truckAnimation);

    while (running && !gameOver)
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            drawHelicopter(helicopterX, helicopterY, helicopterMovingRight);
            drawDeposit(depositX, depositY);
            // Exibir informações
            mvprintw(0, 0, "Mísseis do helicóptero: %d/%d     ", helicopterMissiles.load(), MAX_HELICOPTER_MISSILES);
            mvprintw(1, 0, "Mísseis do depósito: %d/%d        ", depotMissiles.load(), MAX_DEPOT_MISSILES);
        }

        int ch = getch();

        {
            std::lock_guard<std::mutex> lock(mtx);
            eraseHelicopter(helicopterX, helicopterY);
        }

        switch (ch)
        {
        case KEY_UP:
            if (helicopterY > 0)
                helicopterY--;
            break;
        case KEY_DOWN:
            if (helicopterY < height - 2)
                helicopterY++;
            break;
        case KEY_LEFT:
            if (helicopterX > 0)
            {
                helicopterX--;
                helicopterMovingRight = false;
            }
            break;
        case KEY_RIGHT:
            if (helicopterX < width - 9)
            {
                helicopterX++;
                helicopterMovingRight = true;
            }
            break;
        case ' ': // Disparar míssil
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (helicopterMissiles > 0)
            {
                helicopterMissiles--;
                Missile missile = {helicopterX + (helicopterMovingRight ? 9 : -1), helicopterY, true, helicopterMovingRight};
                missileThreads.push_back(std::thread(missileThread, missile));
            }
            else
            {
                mvprintw(2, 0, "Sem mísseis! Reabasteça no depósito.           ");
            }
        }
        break;
        case 'q': // Sair do programa
            running = false;
            break;
        }

        // Verificar se o helicóptero está no depósito e gerenciar recarregamento
        if (isHelicopterAtDepot())
        {
            if (helicopterState == HelicopterState::Normal)
            {
                // Tentar iniciar o recarregamento
                std::unique_lock<std::mutex> lock(depotMutex);
                if ((depotMissiles.load() > 0) && !truckUnloading)
                {
                    helicopterReloading = true;
                    helicopterState = HelicopterState::Reloading;
                    reloadStartTime = std::chrono::steady_clock::now();
                    mvprintw(2, 0, "Recarregando...                                       ");
                }
                else
                {
                    mvprintw(2, 0, "Aguardando para recarregar...                         ");
                }
            }
            else if (helicopterState == HelicopterState::Reloading)
            {
                // Verificar se o tempo de recarregamento passou
                auto now = std::chrono::steady_clock::now();
                if (now - reloadStartTime >= HELICOPTER_RELOAD_TIME)
                {
                    // Finalizar recarregamento
                    std::unique_lock<std::mutex> lock(depotMutex);

                    int neededMissiles = MAX_HELICOPTER_MISSILES - helicopterMissiles.load();
                    int missilesToLoad = std::min(neededMissiles, depotMissiles.load());
                    depotMissiles.fetch_sub(missilesToLoad);
                    helicopterMissiles.fetch_add(missilesToLoad);

                    helicopterReloading = false;
                    helicopterState = HelicopterState::Normal;

                    // Notificar condições
                    depotNotFull.notify_all();
                    depotNotEmpty.notify_all();

                    mvprintw(2, 0, "Recarregamento concluído.                             ");
                }
                else
                {
                    // Ainda recarregando
                    mvprintw(2, 0, "Recarregando...                                       ");
                }
            }
        }
        else
        {
            // Se o helicóptero sair do depósito durante o recarregamento
            if (helicopterState == HelicopterState::Reloading)
            {
                std::unique_lock<std::mutex> lock(depotMutex);
                helicopterReloading = false;
                helicopterState = HelicopterState::Normal;
                depotNotFull.notify_all();
                depotNotEmpty.notify_all();
                mvprintw(2, 0, "Recarregamento cancelado.                             ");
            }
        }
    }

    running = false;

    for (auto &t : missileThreads)
        if (t.joinable())
            t.join();

    if (dinoThread.joinable())
        dinoThread.join();
    if (spawnThread.joinable())
        spawnThread.join();
    if (truckThread.joinable())
        truckThread.join();

    endwin();
    return 0;
}
