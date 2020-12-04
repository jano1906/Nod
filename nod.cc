#include <iostream>
#include <vector>
#include <utility>
#include <regex>
#include <unordered_map>
#include <set>
#include <string>

// --- Macros ---
#define ROADN_ID -1

// --- Regexes ---
// we do those regexes like that to avoid exception throw during static variables initiation
// (std::string = ... may throw an exception) [this is a warning my IDE told me]
namespace regex {
    using regexLiteral = std::string;

    regexLiteral alphabet() {
        static const regexLiteral r = "[[:alnum:]]";
        return r;
    }

    regexLiteral car() {
        static const regexLiteral r = alphabet() + "{3,11}";
        return r;
    }

    regexLiteral road() {
        static const regexLiteral r = "[AS][1-9][0-9]{0,2}";
        return r;
    }

    regexLiteral k() {
        static const regexLiteral r = "[1-9][0-9]*|0";
        return r;
    }

    regexLiteral m() {
        static const regexLiteral r = "[0-9]";
        return r;
    }

    // makes object to match with from string literal
    regexLiteral O(const regexLiteral &r) {
        return "(" + r + ")";
    }
}

// --- Enum types ---
enum ReadType {
    readErr_t, readEmpty_t, readQuery_t, readAction_t
};
enum QueryType {
    queryAll_t, queryRoad_t, queryCar_t, queryMix_t, queryIgnore_t, queryError_t
};
enum RoadType {
    roadA_t, roadS_t, roadN_t
};

// --- Line ---
using LineCounter = uint64_t;
using LineId = uint64_t;

// --- Error() ---
void error(const std::string &line, const LineCounter lC) {
    std::cerr << "Error in line " << lC << ": " << line << std::endl;
}

// --- Car ---
using Car = std::string;

bool validCarName(const std::string &s) {
    static const std::regex carRegex(regex::car());
    return std::regex_match(s, carRegex);
}

// --- Road ---
using RoadId = uint16_t;
using Road = std::pair<RoadType, RoadId>;

bool validRoadName(const std::string &s) {
    static const std::regex roadRegex(regex::road());
    return std::regex_match(s, roadRegex);
}

Road stringToRoad(const std::string &s) {
    if (!validRoadName(s))
        return {roadN_t, ROADN_ID};

    RoadId id = std::stoul(s.substr(1, s.length()));
    switch (s[0]) {
        case 'A':
            return {roadA_t, id};
        case 'S':
            return {roadS_t, id};
        default:
            return {roadN_t, ROADN_ID};
    }
}

std::string roadToString(const Road &road) {
    switch (road.first) {
        case roadA_t:
            return "A" + std::to_string(road.second);
        case roadS_t:
            return "S" + std::to_string(road.second);
        case roadN_t:
            return "";
    }
    return "";
}

// --- Km ---
using K = uint32_t;
using M = uint8_t;
using Km = std::pair<K, M>;
using BigKm = uint64_t;

BigKm kmToBig(const Km &km) {
    return 10 * km.first + km.second;
}

Km bigToKm(const BigKm &big) {
    return {static_cast<K>(big / 10), static_cast<M>(big % 10)};
}

Km diff(const Km &k1, const Km &k2) {
    BigKm bigK1 = kmToBig(k1);
    BigKm bigK2 = kmToBig(k2);
    BigKm diff = bigK1 >= bigK2 ? bigK1 - bigK2 : bigK2 - bigK1;

    return bigToKm(diff);
}

Km sum(const Km &k1, const Km &k2) {
    BigKm bigK1 = kmToBig(k1);
    BigKm bigK2 = kmToBig(k2);
    BigKm sum = bigK1 + bigK2;

    return bigToKm(sum);
}

std::string kmToString(const Km &km) {
    return std::to_string(km.first) + "," + std::to_string(km.second);
}

// --- (Road, Km) -> Position ---
using Position = std::pair<Road, Km>;

// --- ParsedAction ---
using ParsedAction = std::tuple<Car, Road, Km, LineId>;

Car getCar(const ParsedAction &pA) {
    return std::get<0>(pA);
}

Road getRoad(const ParsedAction &pA) {
    return std::get<1>(pA);
}

Km getKm(const ParsedAction &pA) {
    return std::get<2>(pA);
}

LineId getLineId(const ParsedAction &pA) {
    return std::get<3>(pA);
}

// --- LastCarPositionMap ---
// map that stores information about last position of a car
// and number of a line in which this information was added
using LastCarPositionMap = std::unordered_map<Car, std::pair<Position, LineId>>;

void insertCar(LastCarPositionMap &lastCarPositionMap,
               const ParsedAction &pA) {
    Car car = getCar(pA);
    Position pos = {getRoad(pA), getKm(pA)};
    LineId lId = getLineId(pA);
    std::pair<Position, LineId> val = {pos, lId};
    auto search = lastCarPositionMap.find(car);
    if (search == lastCarPositionMap.end()) {
        lastCarPositionMap.insert({car, val});
        return;
    }
    search->second = val;
}

// --- HistoryMap ---
// map with entries <line_number, line> that stores all lines that
// can cause error message to arise while performing an action()
using HistoryMap = std::map<LineId, std::string>;

// --- (LastCarPositionMap, HistoryMap) -> HistoryData ---
using HistoryData = std::pair<LastCarPositionMap &, HistoryMap &>;

LastCarPositionMap &getLastCarPositionMap(const HistoryData &historyData) {
    return std::get<0>(historyData);
}

HistoryMap &getHistoryMap(const HistoryData &historyData) {
    return std::get<1>(historyData);
}

// --- CarDistanceMaps ---
// maps that store total distance a car made on given road type
using CarDistanceOnAMap = std::unordered_map<Car, Km>;
using CarDistanceOnSMap = std::unordered_map<Car, Km>;

void updateCarKmMap(std::unordered_map<Car, Km> &map,
                    const Km &dist, const std::string &s) {
    auto search = map.find(s);
    if (search == map.end()) {
        map.insert({s, dist});
        return;
    }
    search->second = sum(dist, search->second);
}

// --- RoadDistanceMap ---
// map that stores total distance made by all cars on a road
struct roadCmp {
    bool operator()(const Road &r1, const Road &r2) const {
        if (r1.second == r2.second)
            return r1.first < r2.first;
        return r1.second < r2.second;
    }
};

using RoadDistanceMap = std::map<Road, Km, roadCmp>;

void updateRoadDistanceMap(RoadDistanceMap &map,
                           const Km &dist, const Road &road) {
    auto search = map.find(road);
    if (search == map.end()) {
        map.insert({road, dist});
        return;
    }
    search->second = sum(dist, search->second);
}

// --- CarSet ---
// set of cars that occurred while program is running
using CarSet = std::set<Car>;

void updateCarSet(CarSet &carSet, const Car &car) {
    carSet.insert(car);
}

// (CarDistanceMaps, CarSet, RoadDistanceMap) -> StatsTuple ---
using StatsTuple = std::tuple<CarDistanceOnAMap &, CarDistanceOnSMap &,
        CarSet &, RoadDistanceMap &>;

CarDistanceOnAMap &getCarDistanceOnAMap(const StatsTuple &statsTuple) {
    return std::get<0>(statsTuple);
}

CarDistanceOnSMap &getCarDistanceOnSMap(const StatsTuple &statsTuple) {
    return std::get<1>(statsTuple);
}

CarSet &getCarSet(const StatsTuple &statsTuple) {
    return std::get<2>(statsTuple);
}

RoadDistanceMap &getRoadDistanceMap(const StatsTuple &statsTuple) {
    return std::get<3>(statsTuple);
}

void updateStats(StatsTuple &statsTuple, const ParsedAction &pA,
                 const Km &endKm) {
    Car car = getCar(pA);
    Road road = getRoad(pA);
    Km startKm = getKm(pA);
    Km dist = diff(startKm, endKm);

    switch (road.first) {
        case roadA_t:
            updateCarKmMap(getCarDistanceOnAMap(statsTuple),
                           dist, car);
            break;
        case roadS_t:
            updateCarKmMap(getCarDistanceOnSMap(statsTuple),
                           dist, car);
            break;
        case roadN_t:
            exit(1);
    }
    updateCarSet(getCarSet(statsTuple), car);
    updateRoadDistanceMap(getRoadDistanceMap(statsTuple), dist, road);
}

// ===== Main Functions =====

// --- Read() ---
// takes raw string as an argument and writes to ParsedLine variable, 
// increments LineCounter
using ParsedLine = std::vector<std::string>;

ReadType read(const std::string &line, ParsedLine &parsedLine,
              LineCounter &lC) {
    lC++;
    if (line.empty())
        return readEmpty_t;

    parsedLine.clear();
    std::smatch matched;

    static const std::regex actionRegex("\\s*" + regex::O(regex::car()) +
                                        "\\s+" + regex::O(regex::road()) +
                                        "\\s+" + regex::O(regex::k()) + "," + regex::O(regex::m()) +
                                        "\\s*");

    static const std::regex queryRegex(R"(\s*\?\s*)" + regex::O(regex::alphabet() + "*") + "\\s*");

    ReadType returnVal;
    if (std::regex_match(line, matched, queryRegex)) {
        returnVal = readQuery_t;
    } else if (std::regex_match(line, matched, actionRegex)) {
        returnVal = readAction_t;
    } else {
        return readErr_t;
    }
    for (size_t i = 1; i < matched.size(); i++)
        parsedLine.push_back(matched[i]);

    return returnVal;
}

// --- Query() ---
// takes raw string and LineCounter as argument which is currently read line 
// and it's number in case error occurred. Rest of arguments are needed to
// compute and print the answer to the query
using InterpretedQuery = std::pair<QueryType, std::string>;

InterpretedQuery interpretQuery(const ParsedLine &parsedLine,
                                const StatsTuple &statsTuple) {
    if (parsedLine[0].empty())
        return {queryAll_t, ""};
    std::string name = parsedLine[0];
    if (!validCarName(name) && !validRoadName(name))
        return {queryError_t, ""};

    bool carB = getCarSet(statsTuple).find(name) !=
                getCarSet(statsTuple).end();
    bool roadB = getRoadDistanceMap(statsTuple).find(stringToRoad(name)) !=
                 getRoadDistanceMap(statsTuple).end();

    if (carB && roadB)
        return {queryMix_t, name};
    if (carB)
        return {queryCar_t, name};
    if (roadB)
        return {queryRoad_t, name};
    return {queryIgnore_t, name};
}

void queryRoad(const Road &road, const StatsTuple &statsTuple) {
    std::string message = roadToString(road);
    auto search = getRoadDistanceMap(statsTuple).find(road);
    message += " " + kmToString(search->second);

    std::cout << message << std::endl;
}

void queryCar(const std::string &name, const StatsTuple &statsTuple) {
    std::string message = name;
    auto searchA = getCarDistanceOnAMap(statsTuple).find(name);
    auto searchS = getCarDistanceOnSMap(statsTuple).find(name);
    if (searchA != getCarDistanceOnAMap(statsTuple).end())
        message += " A " + kmToString(searchA->second);
    if (searchS != getCarDistanceOnSMap(statsTuple).end())
        message += " S " + kmToString(searchS->second);

    std::cout << message << std::endl;
}

void queryAll(const StatsTuple &statsTuple) {
    for (const auto &x : getCarSet(statsTuple))
        queryCar(x, statsTuple);
    for (const auto &x : getRoadDistanceMap(statsTuple))
        queryRoad(x.first, statsTuple);
}

void query(const std::string &line, const ParsedLine &parsedLine,
           const LineCounter lC, StatsTuple &statsTuple) {
    InterpretedQuery iQ = interpretQuery(parsedLine, statsTuple);

    switch (iQ.first) {
        case queryAll_t:
            queryAll(statsTuple);
            break;
        case queryRoad_t:
            queryRoad(stringToRoad(iQ.second), statsTuple);
            break;
        case queryCar_t:
            queryCar(iQ.second, statsTuple);
            break;
        case queryMix_t:
            queryCar(iQ.second, statsTuple);
            queryRoad(stringToRoad(iQ.second), statsTuple);
            break;
        case queryError_t:
            error(line, lC);
            break;
        case queryIgnore_t:
            break;
    }
}

// --- Action() ---
// takes raw string and LineCounter as argument which is currently read line 
// and it's number in case error occurred. Rest of arguments are updated
// due to action effect
ParsedAction parseAction(const ParsedLine &parsedLine, const LineCounter &lC) {
    Car car = parsedLine[0];
    Road road = stringToRoad(parsedLine[1]);
    Km km = {stoul(parsedLine[2]), stoul(parsedLine[3])};
    return {car, road, km, lC};
}

void action(const std::string &line, const ParsedLine &parsedLine,
            const LineCounter &lC, HistoryData &historyData, StatsTuple &statsTuple) {
    ParsedAction pA = parseAction(parsedLine, lC);

    auto search = getLastCarPositionMap(historyData).find(getCar(pA));

    if (search == getLastCarPositionMap(historyData).end()) {
        insertCar(getLastCarPositionMap(historyData), pA);
        getHistoryMap(historyData).insert({lC, line});
        return;
    }

    Position lastPosition = (search->second).first;
    LineId lastLineId = (search->second).second;
    Road lastRoad = lastPosition.first;
    Km lastKm = lastPosition.second;

    if (getRoad(pA) != lastRoad) {
        error(getHistoryMap(historyData).find(lastLineId)->second, lastLineId);
        getHistoryMap(historyData).erase(lastLineId);
        getHistoryMap(historyData).insert({lC, line});
        insertCar(getLastCarPositionMap(historyData), pA);
        return;
    }

    updateStats(statsTuple, pA, lastKm);
    getHistoryMap(historyData).erase(lastLineId);
    getLastCarPositionMap(historyData).erase(getCar(pA));
}

void run() {
    // getline will write to this variable
    std::string line;
    ParsedLine parsedLine;
    LineCounter lC = 0;

    HistoryMap historyMap;
    LastCarPositionMap lastCarPositionMap;
    HistoryData historyData = {lastCarPositionMap, historyMap};

    CarDistanceOnAMap carDistanceOnA;
    CarDistanceOnSMap carDistanceOnS;
    CarSet allCars;
    RoadDistanceMap roadDistance;
    StatsTuple statsTuple = {carDistanceOnA, carDistanceOnS, allCars, roadDistance};

    while (getline(std::cin, line)) {
        ReadType read_t = read(line, parsedLine, lC);

        switch (read_t) {
            case readErr_t:
                error(line, lC);
                break;
            case readQuery_t:
                query(line, parsedLine, lC, statsTuple);
                break;
            case readAction_t:
                action(line, parsedLine, lC, historyData, statsTuple);
                break;
            case readEmpty_t:
                break;
        }
    }
}

int main() {
    run();
    return 0;
}
