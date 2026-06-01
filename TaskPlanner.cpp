#include "TaskPlanner.h"

/**
 * @file TaskPlanner.cpp
 * @brief Implementa la gestion de tareas, validacion de dependencias y persistencia.
 */

#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <fstream>

using namespace std;

/**
 * @brief Funciones auxiliares internas usadas solo por TaskPlanner.cpp.
 */
namespace {
    /**
     * @brief Formatea un ID numerico de tarea como una cadena de tres digitos.
     *
     * @param id ID de tarea.
     * @return ID formateado con ceros a la izquierda.
     */
    string formatTaskId(int id) {
        stringstream ss;
        ss << setfill('0') << setw(3) << id;
        return ss.str();
    }

    /**
     * @brief Construye una ruta de archivo a partir de una carpeta y un nombre de archivo.
     *
     * @param folderPath Ruta de carpeta proporcionada por el llamador.
     * @param fileName Nombre del archivo objetivo.
     * @return Ruta combinada.
     */
    string makePath(const string& folderPath, const string& fileName) {
        if (folderPath.empty() || folderPath == ".") {
            return fileName;
        }

        char last = folderPath[folderPath.length() - 1];

        if (last == '/' || last == '\\') {
            return folderPath + fileName;
        }

        return folderPath + "/" + fileName;
    }

    /**
     * @brief Escapa caracteres de control y el delimitador visible antes de guardar.
     * @param text Texto original.
     * @return Cadena con caracteres especiales escapados.
     */
    string escapeField(const string& text) {
        string result = "";
        for (char c : text) {
            if (c == '|') result += "\\|"; // Escapa el nuevo delimitador si existe en el texto
            else if (c == '\n') result += "\\n";
            else result += c;
        }
        return result;
    }

    /**
     * @brief Restaura caracteres escapados despues de leer un campo de texto desde disco.
     *
     * @param value Valor escapado del campo.
     * @return Valor del campo sin escapes.
     */
    string unescapeField(const string& value) {
        string unescaped;

        for (size_t i = 0; i < value.length(); i++) {
            if (value[i] == '\\' && i + 1 < value.length()) {
                char next = value[i + 1];

                if (next == '|') {
                    unescaped += '|';
                    i++;
                } else if (next == 'n') {
                    unescaped += '\n';
                    i++;
                } else if (next == '\\') {
                    unescaped += '\\';
                    i++;
                } else {
                    unescaped += value[i];
                }
            } else {
                unescaped += value[i];
            }
        }
        return unescaped;
    }

    /**
     * @brief Divide una cadena de texto utilizando el delimitador visible '|'.
     * @param line Línea de texto leída del archivo.
     * @return Vector de cadenas con los campos individuales.
     */
    vector<string> splitFields(const string& line) {
        vector<string> tokens;
        string token = "";
        bool escaped = false;

        for (size_t i = 0; i < line.length(); ++i) {
            char c = line[i];
            if (escaped) {
                token += c;
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
                token += c;
            } else if (c == '|') {
                tokens.push_back(token);
                token = "";
            } else {
                token += c;
            }
        }
        tokens.push_back(token);
        return tokens;
        }
    }

    /**
     * @brief Convierte una cadena a entero y rechaza caracteres sobrantes.
     *
     * @param value Texto que se convertira.
     * @param result Parametro de salida donde se almacena el entero convertido.
     * @return true si la conversion es exacta; de lo contrario false.
     */
    bool parseInt(const string& value, int& result) {
        stringstream ss(value);
        char extra;

        if (!(ss >> result)) {
            return false;
        }

        return !(ss >> extra);
    }

    /**
     * @brief Valida fechas aceptadas por el dominio del planificador.
     *
     * @param month Numero de mes.
     * @param day Numero de dia.
     * @param year Numero de anio.
     * @return true si la fecha es real y esta entre 2026 y 2028; de lo contrario false.
     */
    bool isValidPlannerDate(int month, int day, int year) {
        return year >= 2026 && year <= 2028
            && month >= 1 && month <= 12
            && day >= 1 && day <= daysInMonth(month, year);
    }

    /**
     * @brief Verifica si una coleccion de tareas cargadas contiene un ID.
     *
     * @param tasks Tareas cargadas desde almacenamiento.
     * @param id ID de tarea que se buscara.
     * @return true si el ID existe en el vector; de lo contrario false.
     */
    bool containsLoadedTask(const vector<Task>& tasks, int id) {
        for (size_t i = 0; i < tasks.size(); i++) {
            if (tasks[i].id == id) return true;
        }
        return false;
    }

/**
 * @brief Verifica si existe un camino en un grafo temporal de validacion.
 *
 * @param graph Grafo de dependencias que se esta validando.
 * @param currentId ID actual explorado por DFS.
 * @param targetId ID objetivo que se busca alcanzar.
 * @param visited IDs ya visitados durante este DFS.
 * @return true si se puede alcanzar el objetivo; de lo contrario false.
 */
bool graphHasPath(const map<int, vector<int> >& graph, int currentId, int targetId, vector<int>& visited) {
    if (currentId == targetId) return true;

    if (find(visited.begin(), visited.end(), currentId) != visited.end())
        return false;

    visited.push_back(currentId);

    map<int, vector<int> >::const_iterator it = graph.find(currentId);

    if (it == graph.end()) return false;

    for (size_t i = 0; i < it->second.size(); i++) {
        if (graphHasPath(graph, it->second[i], targetId, visited)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Inicializa cada posicion de la tabla hash como vacia.
 */
TaskPlanner::TaskPlanner() {
    for (int i = 0; i < HASH_SIZE; i++) {
        hashTable[i] = nullptr;
    }
}

/**
 * @brief Libera los nodos de la tabla hash.
 *
 * No elimina objetos Task porque la lista enlazada es propietaria de los datos de tareas.
 */
TaskPlanner::~TaskPlanner() {
    clearHashTable();
}

/**
 * @brief Convierte un ID de tarea en un indice valido de la tabla hash.
 *
 * @param key ID de tarea.
 * @return Indice de cubeta de la tabla hash.
 */
int TaskPlanner::hashFunction(int key) const {
    int index = key % HASH_SIZE;

    if (index < 0) {
        index += HASH_SIZE;
    }

    return index;
}

/**
 * @brief Libera todos los nodos de la tabla hash.
 */
void TaskPlanner::clearHashTable() {
    for (int i = 0; i < HASH_SIZE; i++) {
        HashNode* current = hashTable[i];

        while (current != nullptr) {
            HashNode* nextNode = current->next;
            delete current;
            current = nextNode;
        }

        hashTable[i] = nullptr;
    }
}

/**
 * @brief Inserta una referencia de tarea en la tabla hash.
 *
 * La llave es el ID de tarea y el valor es un puntero a la tarea almacenada en la lista enlazada.
 *
 * @param id ID de tarea.
 * @param taskPtr Puntero a la tarea almacenada en la lista enlazada.
 * @return true si la referencia de tarea fue insertada; de lo contrario false.
 */
bool TaskPlanner::insertHash(int id, Task* taskPtr) {
    if (taskPtr == nullptr) {
        return false;
    }

    if (searchHash(id) != nullptr) {
        return false;
    }

    int index = hashFunction(id);

    HashNode* newNode = new HashNode;
    newNode->key = id;
    newNode->value = taskPtr;
    newNode->next = hashTable[index];

    hashTable[index] = newNode;

    return true;
}

/**
 * @brief Busca una tarea por ID usando la tabla hash.
 *
 * @param id ID de tarea que se buscara.
 * @return Puntero a la tarea si se encuentra; de lo contrario nullptr.
 */
Task* TaskPlanner::searchHash(int id) const {
    int index = hashFunction(id);

    HashNode* current = hashTable[index];

    while (current != nullptr) {
        if (current->key == id) {
            return current->value;
        }

        current = current->next;
    }

    return nullptr;
}

/**
 * @brief Elimina una referencia de tarea de la tabla hash.
 *
 * Esto no elimina el objeto Task real.
 *
 * @param id ID de tarea que se eliminara de la tabla hash.
 * @return true si se elimino un nodo hash; de lo contrario false.
 */
bool TaskPlanner::removeHash(int id) {
    int index = hashFunction(id);

    HashNode* current = hashTable[index];
    HashNode* previous = nullptr;

    while (current != nullptr) {
        if (current->key == id) {
            if (previous == nullptr) {
                hashTable[index] = current->next;
            } else {
                previous->next = current->next;
            }

            delete current;
            return true;
        }

        previous = current;
        current = current->next;
    }

    return false;
}

/**
 * @brief Verifica si una dependencia ya existe en el grafo.
 *
 * @param taskId ID de la tarea dependiente.
 * @param prerequisiteId ID de la tarea prerrequisito.
 * @return true si la dependencia directa existe; de lo contrario false.
 */
bool TaskPlanner::dependencyExists(int taskId, int prerequisiteId) const {
    map<int, vector<int> >::const_iterator it = dependencyGraph.find(taskId);

    if (it == dependencyGraph.end()) {
        return false;
    }

    const vector<int>& prerequisites = it->second;
    return find(prerequisites.begin(), prerequisites.end(), prerequisiteId) != prerequisites.end();
}

/**
 * @brief Verifica si hay un camino entre dos tareas en el grafo de dependencias.
 *
 * Se usa para evitar ciclos antes de insertar una nueva dependencia.
 *
 * @param startId ID de tarea inicial.
 * @param targetId ID de tarea objetivo.
 * @return true si se puede alcanzar el objetivo; de lo contrario false.
 */
bool TaskPlanner::hasPath(int startId, int targetId) const {
    vector<int> visited;
    return hasPathDFS(startId, targetId, visited);
}

/**
 * @brief Auxiliar DFS recursivo usado para encontrar un camino en el grafo de dependencias.
 *
 * @param currentId ID de tarea actual que se esta explorando.
 * @param targetId ID de tarea objetivo.
 * @param visited IDs que ya fueron visitados.
 * @return true si se puede alcanzar el objetivo; de lo contrario false.
 */
bool TaskPlanner::hasPathDFS(int currentId, int targetId, vector<int>& visited) const {
    if (currentId == targetId) {
        return true;
    }

    if (find(visited.begin(), visited.end(), currentId) != visited.end()) {
        return false;
    }

    visited.push_back(currentId);

    map<int, vector<int> >::const_iterator it = dependencyGraph.find(currentId);

    if (it == dependencyGraph.end()) {
        return false;
    }

    const vector<int>& prerequisites = it->second;

    for (size_t i = 0; i < prerequisites.size(); i++) {
        if (hasPathDFS(prerequisites[i], targetId, visited)) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Elimina toda arista del grafo que apunta a una tarea eliminada o parte de ella.
 *
 * @param id ID de la tarea eliminada.
 */
void TaskPlanner::removeDependencyReferences(int id) {
    dependencyGraph.erase(id);

    for (map<int, vector<int> >::iterator it = dependencyGraph.begin(); it != dependencyGraph.end(); ++it) {
        vector<int>& prerequisites = it->second;
        prerequisites.erase(remove(prerequisites.begin(), prerequisites.end(), id), prerequisites.end());
    }
}

/**
 * @brief Limpia tareas, tabla hash, grafo, historial y cola de pendientes.
 */
void TaskPlanner::clearAllData() {
    taskList.clear();
    clearHashTable();
    dependencyGraph.clear();

    while (!actionHistory.empty()) {
        actionHistory.pop();
    }

    queue<int> emptyQueue;
    pendingTasks.swap(emptyQueue);
}

/**
 * @brief Agrega una tarea leida desde archivo despues de validar sus campos.
 *
 * @param task Tarea cargada desde almacenamiento.
 * @return true si la tarea fue aceptada; de lo contrario false.
 */
bool TaskPlanner::addLoadedTask(const Task& task) {
    if (task.id < 1 || task.id > 999 || task.priority < 1 || task.priority > 3
        || !isValidPlannerDate(task.deadline.mm, task.deadline.dd, task.deadline.yy)
        || task.title.empty() || task.courseName.empty()
        || searchHash(task.id) != nullptr) {
        return false;
    }

    taskList.insertFront(task);
    insertHash(task.id, taskList.searchByID(task.id));

    if (!task.completed) {
        pendingTasks.push(task.id);
    }

    return true;
}

/**
 * @brief Agrega una dependencia leida desde archivo despues de validarla.
 *
 * @param taskId ID de la tarea dependiente.
 * @param prerequisiteId ID de la tarea prerrequisito.
 * @return true si la dependencia fue aceptada; de lo contrario false.
 */
bool TaskPlanner::addLoadedDependency(int taskId, int prerequisiteId) {
    if (taskId == prerequisiteId
        || searchTaskByID(taskId) == nullptr
        || searchTaskByID(prerequisiteId) == nullptr
        || dependencyExists(taskId, prerequisiteId)
        || hasPath(prerequisiteId, taskId)) {
        return false;
    }

    dependencyGraph[taskId].push_back(prerequisiteId);
    return true;
}

/**
 * @brief Agrega una nueva tarea al planificador.
 *
 * Primero se usa la tabla hash para verificar si el ID ya existe.
 * Si el ID es unico, la tarea se inserta en la lista enlazada y luego se indexa en la tabla hash.
 *
 * @param id ID unico de la tarea.
 * @param title Titulo de la tarea.
 * @param desc Descripcion opcional de la tarea.
 * @param course Nombre del curso.
 * @param prio Prioridad de 1 a 3.
 * @param m Mes de la fecha limite.
 * @param d Dia de la fecha limite.
 * @param y Anio de la fecha limite.
 * @return true si la tarea fue agregada; de lo contrario false.
 */
bool TaskPlanner::addTask(int id, string title, string desc, string course, int prio, int m, int d, int y) {
    if (id < 1 || id > 999 || prio < 1 || prio > 3 || !isValidPlannerDate(m, d, y)
        || title.empty() || course.empty()) {
        return false;
    }

    if (searchHash(id) != nullptr) {
        return false;
    }

    Date dl = {m, d, y};
    Task newTask(id, title, desc, course, prio, dl);

    taskList.insertFront(newTask);

    Task* addedTaskPtr = taskList.searchByID(id);
    insertHash(id, addedTaskPtr);

    actionHistory.push("Added [Task " + formatTaskId(id) + " - " + title + "]");
    pendingTasks.push(id);

    return true;
}

/**
 * @brief Muestra todas las tareas del planificador.
 */
void TaskPlanner::displayTasks() const {
    taskList.displayAll();
}

/**
 * @brief Busca una tarea por ID usando la tabla hash.
 *
 * Esto hace que la busqueda sea mas eficiente que recorrer la lista enlazada cada vez.
 *
 * @param id ID de tarea que se buscara.
 * @return Puntero a la tarea si se encuentra; de lo contrario nullptr.
 */
Task* TaskPlanner::searchTaskByID(int id) const {
    return searchHash(id);
}

/**
 * @brief Elimina una tarea del planificador.
 *
 * La tarea se elimina tanto de la lista enlazada como del indice hash.
 *
 * @param id ID de tarea que se eliminara.
 * @return true si la tarea fue eliminada; de lo contrario false.
 */
bool TaskPlanner::removeTask(int id) {
    Task* taskToRemove = searchTaskByID(id);

    if (taskToRemove == nullptr) {
        return false;
    }

    string title = taskToRemove->title;

    if (taskList.removeByID(id)) {
        removeHash(id);
        removeDependencyReferences(id);

        actionHistory.push("Deleted [Task " + formatTaskId(id) + " - " + title + "]");
        return true;
    }

    return false;
}

/**
 * @brief Marca una tarea como completada.
 *
 * La tarea se encuentra usando la tabla hash.
 *
 * @param id ID de tarea que se completara.
 * @return true si la tarea fue marcada como completada; de lo contrario false.
 */
bool TaskPlanner::markTaskCompleted(int id) {
    Task* task = searchTaskByID(id);

    if (task == nullptr) {
        return false;
    }

    if (!canTaskBeCompleted(id)) {
        return false;
    }

    task->completed = true;

    actionHistory.push("Completed [Task " + formatTaskId(id) + " - " + task->title + "]");
    return true;
}

/**
 * @brief Agrega una arista dirigida al grafo de dependencias.
 *
 * La arista taskId -> prerequisiteId significa que taskId depende de prerequisiteId.
 *
 * @param taskId ID de la tarea dependiente.
 * @param prerequisiteId ID de la tarea prerrequisito.
 * @return true si la dependencia fue registrada; de lo contrario false.
 */
bool TaskPlanner::addDependency(int taskId, int prerequisiteId) {
    if (taskId == prerequisiteId) {
        return false;
    }

    if (searchTaskByID(taskId) == nullptr || searchTaskByID(prerequisiteId) == nullptr) {
        return false;
    }

    if (dependencyExists(taskId, prerequisiteId)) {
        return false;
    }

    // Si el prerrequisito ya alcanza la tarea, agregar esta arista cerraria un ciclo.
    if (hasPath(prerequisiteId, taskId)) {
        return false;
    }

    dependencyGraph[taskId].push_back(prerequisiteId);

    actionHistory.push("Added dependency [Task " + formatTaskId(taskId)
                       + " depends on Task " + formatTaskId(prerequisiteId) + "]");

    return true;
}

/**
 * @brief Muestra el grafo de dependencias como una lista de adyacencia textual.
 */
void TaskPlanner::displayDependencyGraph() const {
    if (dependencyGraph.empty()) {
        cout << "\n * No task dependencies registered. *\n";
        return;
    }

    cout << "\n Dependency graph (task -> prerequisites):\n";

    for (map<int, vector<int> >::const_iterator it = dependencyGraph.begin(); it != dependencyGraph.end(); ++it) {
        Task* task = searchTaskByID(it->first);

        if (task == nullptr || it->second.empty()) {
            continue;
        }

        cout << " [Task " << setfill('0') << setw(3) << task->id << " - " << task->title << "] -> ";

        const vector<int>& prerequisites = it->second;

        for (size_t i = 0; i < prerequisites.size(); i++) {
            Task* prerequisite = searchTaskByID(prerequisites[i]);

            if (prerequisite != nullptr) {
                cout << "[Task " << setfill('0') << setw(3) << prerequisite->id
                     << " - " << prerequisite->title << "]";
            }

            if (i + 1 < prerequisites.size()) {
                cout << ", ";
            }
        }

        cout << '\n';
    }

    cout << setfill(' ');
}

/**
 * @brief Verifica si una tarea existe y todos sus prerrequisitos directos estan completados.
 *
 * @param id ID de tarea que se verificara.
 * @return true si la tarea puede completarse; de lo contrario false.
 */
bool TaskPlanner::canTaskBeCompleted(int id) const {
    if (searchTaskByID(id) == nullptr) {
        return false;
    }

    return getUncompletedPrerequisites(id).empty();
}

/**
 * @brief Obtiene los prerrequisitos directos que todavia no estan completados.
 *
 * @param id ID de tarea que se inspeccionara.
 * @return IDs de tareas prerrequisito pendientes.
 */
vector<int> TaskPlanner::getUncompletedPrerequisites(int id) const {
    vector<int> pendingPrerequisites;
    map<int, vector<int> >::const_iterator it = dependencyGraph.find(id);

    if (it == dependencyGraph.end()) {
        return pendingPrerequisites;
    }

    const vector<int>& prerequisites = it->second;

    for (size_t i = 0; i < prerequisites.size(); i++) {
        Task* prerequisite = searchTaskByID(prerequisites[i]);

        if (prerequisite != nullptr && !prerequisite->completed) {
            pendingPrerequisites.push_back(prerequisites[i]);
        }
    }

    return pendingPrerequisites;
}

/**
 * @brief Obtiene cada prerrequisito directo registrado para una tarea.
 *
 * @param id ID de tarea que se inspeccionara.
 * @return IDs de tareas prerrequisito directas.
 */
vector<int> TaskPlanner::getPrerequisites(int id) const {
    map<int, vector<int> >::const_iterator it = dependencyGraph.find(id);

    if (it == dependencyGraph.end()) {
        return vector<int>();
    }

    return it->second;
}

/**
 * @brief Guarda el estado del planificador en archivos de tareas, historial y dependencias.
 *
 * @param folderPath Carpeta donde se escribiran los archivos.
 * @return true si todos los archivos se guardaron correctamente; de lo contrario false.
 */
bool TaskPlanner::saveData(const string& folderPath) const {
    ofstream tasksFile(makePath(folderPath, "tasks.txt").c_str());

    if (!tasksFile) return false;

    for (const Task& task : taskList) {
        tasksFile << task.id << "|"
                  << task.priority << "|"
                  << escapeField(task.title) << "|"
                  << escapeField(task.description) << "|"
                  << escapeField(task.courseName) << "|"
                  << task.deadline.mm << "|"
                  << task.deadline.dd << "|"
                  << task.deadline.yy << "|"
                  << (task.completed ? "1" : "0") << "\n";
    }
    tasksFile.close();

    ofstream historyFile(makePath(folderPath, "history.txt").c_str());

    if (!historyFile) {
        return false;
    }

    stack<string> historyCopy = actionHistory;

    while (!historyCopy.empty()) {
        historyFile << escapeField(historyCopy.top()) << '\n';
        historyCopy.pop();
    }

    ofstream dependencyFile(makePath(folderPath, "dependencies.txt").c_str());

    if (!dependencyFile) {
        return false;
    }

    for (map<int, vector<int> >::const_iterator it = dependencyGraph.begin(); it != dependencyGraph.end(); ++it) {
        const vector<int>& prerequisites = it->second;

        for (size_t i = 0; i < prerequisites.size(); i++) {
            dependencyFile << it->first << '|' << prerequisites[i] << '\n';
        }
    }

    return true;
}

/**
 * @brief Carga el estado del planificador desde archivos de tareas, historial y dependencias.
 *
 * El metodo valida toda la entrada antes de reemplazar los datos actuales en memoria.
 *
 * @param folderPath Carpeta desde donde se leeran los archivos.
 * @return true si todos los archivos fueron validos y cargados; de lo contrario false.
 */
bool TaskPlanner::loadData(const string& folderPath) {
    ifstream taskFile(makePath(folderPath, "tasks.txt").c_str());
    ifstream historyFile(makePath(folderPath, "history.txt").c_str());
    ifstream dependencyFile(makePath(folderPath, "dependencies.txt").c_str());

    if (!taskFile || !historyFile || !dependencyFile) {
        return false;
    }

    vector<Task> loadedTasks;
    vector<string> loadedHistory;
    vector<pair<int, int> > loadedDependencies;
    string line;

    while (getline(taskFile, line)) {
        if (line.empty()) {
            continue;
        }

        vector<string> fields = splitFields(line);

        if (fields.size() != 9) {
            return false;
        }

        int id;
        int priority;
        int month;
        int day;
        int year;
        int completed;

        if (!parseInt(fields[0], id)
            || !parseInt(fields[1], priority)
            || !parseInt(fields[2], month)
            || !parseInt(fields[3], day)
            || !parseInt(fields[4], year)
            || !parseInt(fields[5], completed)
            || (completed != 0 && completed != 1)) {
            return false;
        }

        Date deadline = {month, day, year};
        Task task(id, unescapeField(fields[6]), unescapeField(fields[7]), unescapeField(fields[8]), priority, deadline);
        task.completed = (completed == 1);

        if (task.id < 1 || task.id > 999
            || task.priority < 1 || task.priority > 3
            || !isValidPlannerDate(task.deadline.mm, task.deadline.dd, task.deadline.yy)
            || task.title.empty() || task.courseName.empty()) {
            return false;
        }

        for (size_t i = 0; i < loadedTasks.size(); i++) {
            if (loadedTasks[i].id == task.id) {
                return false;
            }
        }

        loadedTasks.push_back(task);
    }

    while (getline(historyFile, line)) {
        loadedHistory.push_back(unescapeField(line));
    }

    while (getline(dependencyFile, line)) {
        if (line.empty()) {
            continue;
        }

        vector<string> fields = splitFields(line);

        if (fields.size() != 2) {
            return false;
        }

        int taskId;
        int prerequisiteId;

        if (!parseInt(fields[0], taskId) || !parseInt(fields[1], prerequisiteId)) {
            return false;
        }

        loadedDependencies.push_back(make_pair(taskId, prerequisiteId));
    }

    map<int, vector<int> > validationGraph;

    for (vector<pair<int, int> >::const_iterator it = loadedDependencies.begin(); it != loadedDependencies.end(); ++it) {
        int taskId = it->first;
        int prerequisiteId = it->second;

        if (taskId == prerequisiteId
            || !containsLoadedTask(loadedTasks, taskId)
            || !containsLoadedTask(loadedTasks, prerequisiteId)) {
            return false;
        }

        vector<int>& prerequisites = validationGraph[taskId];

        if (find(prerequisites.begin(), prerequisites.end(), prerequisiteId) != prerequisites.end()) {
            return false;
        }

        vector<int> visited;

        if (graphHasPath(validationGraph, prerequisiteId, taskId, visited)) {
            return false;
        }

        prerequisites.push_back(prerequisiteId);
    }

    clearAllData();

    for (vector<Task>::reverse_iterator it = loadedTasks.rbegin(); it != loadedTasks.rend(); ++it) {
        if (!addLoadedTask(*it)) {
            clearAllData();
            return false;
        }
    }

    for (vector<pair<int, int> >::const_iterator it = loadedDependencies.begin(); it != loadedDependencies.end(); ++it) {
        if (!addLoadedDependency(it->first, it->second)) {
            clearAllData();
            return false;
        }
    }

    for (vector<string>::reverse_iterator it = loadedHistory.rbegin(); it != loadedHistory.rend(); ++it) {
        actionHistory.push(*it);
    }

    return true;
}

/**
 * @brief Muestra el historial de acciones realizadas en el planificador.
 */
void TaskPlanner::showActionsHistory() const {
    if (actionHistory.empty()) {
        cout << "\n * Action history is currently empty. *\n";
        return;
    }

    stack<string> tmpStack = actionHistory;

    cout << "\n Action history (newest first):\n";

    while (!tmpStack.empty()) {
        cout << " ~ " << tmpStack.top() << endl;
        tmpStack.pop();
    }
}

/**
 * @brief Muestra las tareas pendientes.
 *
 * La cola almacena IDs de tareas en lugar de punteros a Task.
 * Cada ID se busca en la tabla hash antes de mostrarse.
 */
void TaskPlanner::showPendingTasks() const {
    if (pendingTasks.empty()) {
        cout << "\n * No pending tasks at the moment. *\n";
        return;
    }

    queue<int> tmpQueue = pendingTasks;

    cout << "\n Pending tasks:\n";

    while (!tmpQueue.empty()) {
        int id = tmpQueue.front();
        tmpQueue.pop();

        Task* task = searchTaskByID(id);

        if (task != nullptr && !task->completed) {
            cout << " [Task ID "
                 << setfill('0') << setw(3) << task->id
                 << " - " << task->title << "]\n";
        }
    }

    cout << setfill(' ');
}
