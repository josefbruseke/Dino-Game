#include <ncurses.h>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <atomic>
#include <condition_variable>

// **Constantes e Configurações**
const int width = 100;
const int height = 40;
const int MAX_DEPOT_MISSILES = 10;
const auto HELICOPTER_RELOAD_TIME = std::chrono::seconds(1);

// **Estruturas**
struct Dino
{
    int x, y;         // Posição
    bool alive;       // Vivo ou morto
    bool movingRight; // Direção do movimento
    int headshotHits; // Contador de tiros na cabeça
};

struct Missile
{
    int x, y;         // Posição
    bool active;      // Indica se o míssil está ativo
    bool movingRight; // Direção do movimento
};

// **Variáveis Globais**
std::mutex mtx;        // Mutex para sincronização geral
std::mutex depotMutex; // Mutex para o depósito
std::condition_variable depotNotFull;
std::condition_variable depotNotEmpty;

bool running = true;   // Controle do loop principal
bool gameOver = false; // Estado do jogo
bool truckUnloading = false;
bool helicopterReloading = false;

std::atomic<int> depotMissiles{MAX_DEPOT_MISSILES};
int MAX_HELICOPTER_MISSILES = 10; // Ajustado com base na dificuldade
std::atomic<int> helicopterMissiles;
int m = 1;  // Número de tiros na cabeça para matar o dinossauro
int n = 10; // Capacidade de mísseis do helicóptero
int t = 5;  // Tempo para gerar um novo dinossauro

// **Objetos do Jogo**
std::vector<std::thread> missileThreads; // Threads dos mísseis
std::vector<Dino> dinos;                 // Lista de dinossauros

// **Variáveis do Helicóptero**
int helicopterX = 40, helicopterY = 20;
bool helicopterMovingRight = true;

enum class HelicopterState
{
    Normal,
    Reloading
};
HelicopterState helicopterState = HelicopterState::Normal;
std::chrono::time_point<std::chrono::steady_clock> reloadStartTime;

// **Posição do Depósito**
int depositX = 0, depositY = 10;

// **Representações Gráficas**
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

const char *truckRight[5] = {
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

// **Protótipos das Funções**
void drawSkyAndGrass(int width, int height);
void drawDino(const Dino &dino);
void eraseDino(const Dino &dino);
void drawHelicopter(int x, int y, bool movingRight);
void eraseHelicopter(int x, int y);
void drawMissile(Missile &missile);
void eraseMissile(Missile &missile);
void drawTruck(int x, int y, bool movingRight);
void eraseTruck(int x, int y);
void drawDeposit(int x, int y);
bool isHelicopterAtDepot();
void unloadMissilesToDepot(int amount);
void truckAnimation();
bool checkCollisionWithDinoHead(const Missile &missile, Dino &dino);
bool checkCollisionWithDinoBody(const Missile &missile, const Dino &dino);
bool checkCollisionWithHelicopter(const Dino &dino);
void missileThread(Missile missile);
int countAliveDinos();
void dinoAnimation();
void spawnDino();
void showDifficultyMenu(int &m, int &n, int &t);

// **Implementações das Funções**

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
        if (x >= 0 && x < width)
            mvaddstr(y + i, x, std::string(30, ' ').c_str());
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

void truckAnimation()
{
    int truckWidth = 30; // Largura do caminhão
    int truckX = -truckWidth;
    int truckY = height - 10;
    bool truckMovingRight = true;

    while (running && !gameOver)
    {
        // Caminhão traz mísseis de tempos em tempos
        std::this_thread::sleep_for(std::chrono::seconds(15)); // Tempo entre viagens do caminhão

        // Caminhão entra na tela
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

        // Caminhão sai do depósito pela direita
        while (truckX < width + truckWidth && running && !gameOver)
        {
            {
                std::lock_guard<std::mutex> lock(mtx);
                eraseTruck(truckX, truckY);
                truckX++;
                if (truckX >= 0 && truckX < width)
                    drawTruck(truckX, truckY, truckMovingRight);
                drawDeposit(depositX, depositY);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Limpar qualquer resíduo do caminhão que possa ficar na tela
        {
            std::lock_guard<std::mutex> lock(mtx);
            eraseTruck(truckX - 1, truckY);
        }

        // Reiniciar posição do caminhão para próxima viagem
        truckX = -truckWidth;
    }
}

bool checkCollisionWithDinoHead(const Missile &missile, Dino &dino)
{
    if (!dino.alive)
        return false;

    // Coordenadas da cabeça do dinossauro
    int headX = dino.x + (dino.movingRight ? 14 : 5);
    int headY = dino.y + 1;

    // Verificar colisão com a cabeça
    if (missile.x == headX && missile.y == headY)
    {
        dino.headshotHits++;
        if (dino.headshotHits >= m)
        {
            dino.alive = false;
            eraseDino(dino);
        }
        return true;
    }
    return false;
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
        std::this_thread::sleep_for(std::chrono::seconds(t)); // Intervalo baseado na dificuldade
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (countAliveDinos() < 5)
            {
                int randomHeight = height - 8 - (rand() % 5);
                Dino newDino = {0, randomHeight, true, true, 0};
                dinos.push_back(newDino);
            }
        }
    }
}

void showDifficultyMenu(int &m, int &n, int &t)
{
    clear();
    mvprintw(0, 0, "Escolha o grau de dificuldade:");

    mvprintw(2, 0, "1. Fácil   (m=1, n=20, t=10)");
    mvprintw(3, 0, "2. Médio   (m=2, n=15, t=7)");
    mvprintw(4, 0, "3. Difícil (m=3, n=10, t=5)");
    mvprintw(6, 0, "Escolha (1/2/3): ");

    int choice = 0;
    while (choice < '1' || choice > '3')
    {
        choice = getch();
    }

    switch (choice)
    {
    case '1':
        m = 1;
        n = 20;
        t = 10;
        break;
    case '2':
        m = 2;
        n = 15;
        t = 7;
        break;
    case '3':
        m = 3;
        n = 10;
        t = 5;
        break;
    }

    clear();
    mvprintw(0, 0, "Dificuldade selecionada: %s", (choice == '1' ? "Fácil" : (choice == '2' ? "Médio" : "Difícil")));
    refresh();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    clear();
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

    // Exibir menu de dificuldade
    showDifficultyMenu(m, n, t);

    // Configurar variáveis globais com base na dificuldade escolhida
    MAX_HELICOPTER_MISSILES = n;
    helicopterMissiles.store(MAX_HELICOPTER_MISSILES);

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
            if (helicopterMissiles.load() > 0)
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
                    int missilesToLoad = std::min(neededMissiles, (int)depotMissiles.load());
                    depotMissiles.fetch_sub(missilesToLoad);
                    helicopterMissiles.fetch_add(missilesToLoad);

                    // Garantir que não exceda o máximo
                    if (helicopterMissiles.load() > MAX_HELICOPTER_MISSILES)
                    {
                        helicopterMissiles.store(MAX_HELICOPTER_MISSILES);
                    }
                    if (depotMissiles.load() < 0)
                    {
                        depotMissiles.store(0);
                    }

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
