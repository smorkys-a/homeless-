#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cctype>

using namespace std;

// ---------- helpers ----------
static inline string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static inline string lower(string s) {
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// ---------- domain ----------
struct Player {
    string name;

    string job;
    int age = 0;
    string health;
    string hobby;
    string phobia;
    string item;
    string trait;

    // reveal flags
    bool rJob=false, rAge=false, rHealth=false, rHobby=false, rPhobia=false, rItem=false, rTrait=false;

    bool alive = true;
};

struct Bunker {
    int capacity = 3;         // how many survive
    int foodDays = 45;        // food in days for full team (capacity)
    int waterDays = 45;
    int medsKits = 1;
    bool hasPower = true;
    bool hasInternet = false;
    bool hasVentilation = true;
};

struct Scenario {
    string title;

    // "needs": required skill tags in final team (example: "medical", "engineering")
    unordered_set<string> requiredSkills;

    // forbidden / risky health conditions -> big penalty
    unordered_set<string> forbiddenHealth;

    // minimum resource constraints
    int minFoodDays = 30;
    int minWaterDays = 30;
    int minMeds = 1;
    bool powerRequired = true;
    bool ventilationRequired = true;
};

// job -> skills tags
static unordered_map<string, vector<string>> JOB_SKILLS = {
    {"Doctor", {"medical"}},
    {"Nurse", {"medical"}},
    {"Engineer", {"engineering"}},
    {"Mechanic", {"engineering"}},
    {"Electrician", {"engineering"}},
    {"Programmer", {"tech"}},
    {"Biologist", {"science"}},
    {"Chemist", {"science"}},
    {"Cook", {"food"}},
    {"Farmer", {"food"}},
    {"Soldier", {"security"}},
    {"Police", {"security"}},
    {"Teacher", {"social"}},
    {"Psychologist", {"social"}},
    {"Builder", {"engineering"}},
};

struct Event {
    string title;
    // effects: modify bunker/scenario
    // Using lambdas would be nicer, but keep simple: fields below
    // (deltas / toggles / new requirements)
    int foodDelta=0;
    int waterDelta=0;
    int medsDelta=0;
    int addMinFood=0;
    int addMinWater=0;
    int addMinMeds=0;

    bool powerOff=false;
    bool powerOn=false;

    bool ventilationOff=false;
    bool ventilationOn=false;

    // Add required skill, e.g. "medical"
    string addRequiredSkill;

    // Add forbidden health condition
    string addForbiddenHealth;

    string note;
};

// ---------- game ----------
class Game {
    vector<Player> players;
    Scenario scenario;
    Bunker bunker;

    mt19937 rng{ random_device{}() };

    // pools
    vector<string> jobs = {
        "Doctor","Nurse","Engineer","Mechanic","Electrician","Programmer","Biologist","Chemist",
        "Cook","Farmer","Soldier","Police","Teacher","Psychologist","Builder"
    };
    vector<string> healths = {
        "Healthy","Asthma","Diabetes","Broken leg","Heart disease","Epilepsy","Hypertension","Immune weak"
    };
    vector<string> hobbies = {
        "First aid","Hiking","Chess","Cooking","Electronics","Gardening","Martial arts","Singing","Drawing","Fitness"
    };
    vector<string> phobias = {
        "Claustrophobia","Darkness","Heights","Blood","Insects","Water","Fire","None"
    };
    vector<string> items = {
        "Toolbox","First aid kit","Water filter","Seeds","Laptop","Radio","Knife","Generator parts","Medicine bag","Flashlight"
    };
    vector<string> traits = {
        "Leader","Calm under pressure","Aggressive","Selfish","Team player","Strategist","Panic prone","Charismatic","Stubborn","Quick learner"
    };

    vector<Event> events;

public:
    Game() {
        buildEvents();
        buildDefaultScenario();
    }

    void run() {
        setupPlayers();
        dealCards();

        cout << "\nScenario: " << scenario.title << "\n";
        cout << "Bunker capacity: " << bunker.capacity << " out of 6\n";
        printBunker();

        int round = 1;
        // reveal order for 6 people:
        // R1 job, R2 health, R3 item, R4 age, R5 hobby, R6 phobia, R7 trait ...
        vector<string> revealPlan = {"job","health","item","age","hobby","phobia","trait"};

        while (true) {
            cout << "\n==============================\n";
            cout << "ROUND " << round << "\n";

            // reveal
            string what = revealPlan[min((int)revealPlan.size()-1, round-1)];
            reveal(what);

            // event after reveal (except maybe last)
            applyRandomEvent(round);

            // vote + eliminate if too many alive
            if (aliveCount() > bunker.capacity) {
                voteEliminate();
            }

            // check end
            if (aliveCount() <= bunker.capacity) {
                finalResult();
                break;
            }

            round++;
        }
    }

private:
    void buildDefaultScenario() {
        scenario.title = "Nuclear Winter";
        scenario.requiredSkills = {"medical","engineering","food"};
        scenario.forbiddenHealth = {"Heart disease","Epilepsy"};
        scenario.minFoodDays = 30;
        scenario.minWaterDays = 30;
        scenario.minMeds = 1;
        scenario.powerRequired = true;
        scenario.ventilationRequired = true;

        bunker.capacity = 3;
        bunker.foodDays = 45;
        bunker.waterDays = 45;
        bunker.medsKits = 1;
        bunker.hasPower = true;
        bunker.hasInternet = false;
        bunker.hasVentilation = true;
    }

    void buildEvents() {
        // Keep them impactful but understandable
        events = {
            {"Food storage spoiled", -15, 0, 0, +10, 0, 0, false,false,false,false,"", "", "Now you need more food or a 'food' expert."},
            {"Water leak", 0, -15, 0, 0, +10, 0, false,false,false,false,"", "", "Water becomes critical."},
            {"Generator failure", 0, 0, 0, 0,0,0, true,false,false,false,"engineering","", "Power off unless team has strong engineering."},
            {"Ventilation filter clogged", 0,0,0, 0,0,0, false,false, true,false,"engineering","", "Ventilation is at risk."},
            {"Infection outbreak nearby", 0,0, -1, 0,0, +1, false,false,false,false,"medical","Immune weak", "Medical skill is now required."},
            {"Found medical supplies", 0,0, +2, 0,0,0, false,false,false,false,"", "", "More medkits."},
            {"Found water canisters", 0, +10, 0, 0,0,0, false,false,false,false,"", "", "More water."},
            {"Found canned food", +10, 0,0, 0,0,0, false,false,false,false,"", "", "More food."},
            {"Radio contact: rescue signal", 0,0,0, 0,0,0, false,false,false,false,"tech","", "Tech skill helps establish contact."},
            {"Raiders detected", 0,0,0, 0,0,0, false,false,false,false,"security","", "Security skill becomes required."},
        };
    }

    void setupPlayers() {
        players.clear();
        players.reserve(6);

        cout << "=== BUNKER GAME (6 players) ===\n";
        for (int i = 1; i <= 6; i++) {
            Player p;
            cout << "Enter name for player " << i << ": ";
            getline(cin, p.name);
            p.name = trim(p.name);
            if (p.name.empty()) p.name = "Player" + to_string(i);
            players.push_back(p);
        }
    }

    int randInt(int a, int b) {
        uniform_int_distribution<int> dist(a,b);
        return dist(rng);
    }

    template<class T>
    T pick(const vector<T>& v) {
        return v[randInt(0, (int)v.size()-1)];
    }

    void dealCards() {
        // Give each player random cards (allow repeats between players — так проще)
        for (auto &p : players) {
            p.job = pick(jobs);
            p.age = randInt(18, 60);
            p.health = pick(healths);
            p.hobby = pick(hobbies);
            p.phobia = pick(phobias);
            p.item = pick(items);
            p.trait = pick(traits);
        }
    }

    void printBunker() {
        cout << "\n--- Bunker status ---\n";
        cout << "Food days: " << bunker.foodDays << " (min " << scenario.minFoodDays << ")\n";
        cout << "Water days: " << bunker.waterDays << " (min " << scenario.minWaterDays << ")\n";
        cout << "Medkits: " << bunker.medsKits << " (min " << scenario.minMeds << ")\n";
        cout << "Power: " << (bunker.hasPower ? "YES" : "NO") << (scenario.powerRequired ? " (required)" : "") << "\n";
        cout << "Ventilation: " << (bunker.hasVentilation ? "YES" : "NO") << (scenario.ventilationRequired ? " (required)" : "") << "\n";
        cout << "Internet: " << (bunker.hasInternet ? "YES" : "NO") << "\n";
        cout << "Required skills: ";
        for (auto &s : scenario.requiredSkills) cout << s << " ";
        cout << "\nForbidden health: ";
        for (auto &h : scenario.forbiddenHealth) cout << h << " ";
        cout << "\n---------------------\n";
    }

    int aliveCount() const {
        int c=0;
        for (auto &p : players) if (p.alive) c++;
        return c;
    }

    bool isAliveName(const string& name) const {
        string key = lower(trim(name));
        for (auto &p : players) {
            if (p.alive && lower(p.name) == key) return true;
        }
        return false;
    }

    Player* getAliveByName(const string& name) {
        string key = lower(trim(name));
        for (auto &p : players) {
            if (p.alive && lower(p.name) == key) return &p;
        }
        return nullptr;
    }

    void reveal(const string& what) {
        cout << "\n=== REVEAL: " << what << " ===\n";
        for (auto &p : players) {
            if (!p.alive) continue;

            cout << p.name << " -> ";
            if (what=="job")    { p.rJob=true;    cout << "Job: " << p.job; }
            if (what=="age")    { p.rAge=true;    cout << "Age: " << p.age; }
            if (what=="health") { p.rHealth=true; cout << "Health: " << p.health; }
            if (what=="hobby")  { p.rHobby=true;  cout << "Hobby: " << p.hobby; }
            if (what=="phobia") { p.rPhobia=true; cout << "Phobia: " << p.phobia; }
            if (what=="item")   { p.rItem=true;   cout << "Item: " << p.item; }
            if (what=="trait")  { p.rTrait=true;  cout << "Trait: " << p.trait; }
            cout << "\n";
        }
    }

    void applyRandomEvent(int round) {
        // Event frequency: after every round except when already near end
        if (aliveCount() <= bunker.capacity) return;

        // pick event
        Event e = events[randInt(0, (int)events.size()-1)];
        cout << "\n*** EVENT: " << e.title << " ***\n";
        if (!e.note.empty()) cout << e.note << "\n";

        bunker.foodDays = max(0, bunker.foodDays + e.foodDelta);
        bunker.waterDays = max(0, bunker.waterDays + e.waterDelta);
        bunker.medsKits = max(0, bunker.medsKits + e.medsDelta);

        scenario.minFoodDays = max(0, scenario.minFoodDays + e.addMinFood);
        scenario.minWaterDays = max(0, scenario.minWaterDays + e.addMinWater);
        scenario.minMeds = max(0, scenario.minMeds + e.addMinMeds);

        if (e.powerOff) bunker.hasPower = false;
        if (e.powerOn) bunker.hasPower = true;

        if (e.ventilationOff) bunker.hasVentilation = false;
        if (e.ventilationOn) bunker.hasVentilation = true;

        if (!e.addRequiredSkill.empty()) scenario.requiredSkills.insert(e.addRequiredSkill);
        if (!e.addForbiddenHealth.empty()) scenario.forbiddenHealth.insert(e.addForbiddenHealth);

        printBunker();
    }

    vector<string> listAliveNames() const {
        vector<string> a;
        for (auto &p : players) if (p.alive) a.push_back(p.name);
        return a;
    }

    // vote among alive. returns map name->votes (names are lowercased canonical)
    unordered_map<string,int> collectVotes(const vector<string>& allowedTargetsLower) {
        unordered_set<string> allowed(allowedTargetsLower.begin(), allowedTargetsLower.end());
        unordered_map<string,int> votes;

        auto aliveNames = listAliveNames();
        cout << "\nAlive players:\n";
        for (auto &n : aliveNames) cout << "- " << n << "\n";

        for (auto &voter : players) {
            if (!voter.alive) continue;

            string target;
            while (true) {
                cout << voter.name << ", vote to eliminate: ";
                getline(cin, target);
                target = lower(trim(target));

                if (!allowed.empty()) {
                    if (allowed.count(target)) break;
                } else {
                    if (isAliveName(target)) break;
                }
                cout << "Invalid target. Try again.\n";
            }
            votes[target]++;
        }

        return votes;
    }

    void voteEliminate() {
        cout << "\n=== VOTING ===\n";

        // 1st voting among all alive
        auto votes = collectVotes({}); // allow any alive
        vector<pair<string,int>> sorted;
        for (auto &kv : votes) sorted.push_back(kv);
        sort(sorted.begin(), sorted.end(), [](auto &a, auto &b){
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });

        int best = sorted.front().second;
        vector<string> leaders;
        for (auto &kv : sorted) if (kv.second == best) leaders.push_back(kv.first);

        // tie handling: revote once among leaders if tie and >1 leader
        if (leaders.size() > 1) {
            cout << "\nTie between: ";
            for (auto &x : leaders) cout << x << " ";
            cout << "\nRevote among tied players.\n";

            auto revotes = collectVotes(leaders);
            vector<pair<string,int>> rs;
            for (auto &kv : revotes) rs.push_back(kv);
            sort(rs.begin(), rs.end(), [](auto &a, auto &b){
                if (a.second != b.second) return a.second > b.second;
                return a.first < b.first;
            });

            int rbest = rs.front().second;
            vector<string> rleaders;
            for (auto &kv : rs) if (kv.second == rbest) rleaders.push_back(kv.first);

            string eliminatedLower;
            if (rleaders.size() == 1) {
                eliminatedLower = rleaders.front();
            } else {
                // still tie -> random among tied
                eliminatedLower = rleaders[randInt(0, (int)rleaders.size()-1)];
                cout << "Still tie. Random pick among leaders.\n";
            }

            eliminateByLowerName(eliminatedLower, rbest);
            return;
        }

        // no tie
        eliminateByLowerName(leaders.front(), best);
    }

    void eliminateByLowerName(const string& eliminatedLower, int votesCount) {
        Player* p = getAliveByName(eliminatedLower);
        if (!p) {
            // fallback random alive
            vector<int> idx;
            for (int i=0;i<(int)players.size();i++) if (players[i].alive) idx.push_back(i);
            int r = idx[randInt(0, (int)idx.size()-1)];
            players[r].alive = false;
            cout << "Target not found. Random eliminated: " << players[r].name << "\n";
            return;
        }
        p->alive = false;
        cout << ">>> Eliminated: " << p->name << " (" << votesCount << " votes)\n";
    }

    unordered_set<string> teamSkills(const vector<Player*>& team) {
        unordered_set<string> s;
        for (auto *p : team) {
            auto it = JOB_SKILLS.find(p->job);
            if (it != JOB_SKILLS.end()) {
                for (auto &tag : it->second) s.insert(tag);
            }
            // hobby bonuses (small)
            if (lower(p->hobby) == "first aid") s.insert("medical");
            if (lower(p->hobby) == "electronics") s.insert("engineering");
            if (lower(p->hobby) == "gardening") s.insert("food");
            if (lower(p->hobby) == "martial arts") s.insert("security");
        }
        return s;
    }

    int computeScore(const vector<Player*>& team) {
        // Start score
        int score = 100;

        // Required skills
        auto skills = teamSkills(team);
        for (auto &req : scenario.requiredSkills) {
            if (!skills.count(req)) score -= 25;
        }

        // Resources
        if (bunker.foodDays < scenario.minFoodDays) score -= 20;
        if (bunker.waterDays < scenario.minWaterDays) score -= 20;
        if (bunker.medsKits < scenario.minMeds) score -= 20;

        if (scenario.powerRequired && !bunker.hasPower) score -= 25;
        if (scenario.ventilationRequired && !bunker.hasVentilation) score -= 25;

        // Health penalties
        for (auto *p : team) {
            if (scenario.forbiddenHealth.count(p->health)) score -= 30;
            if (p->health == "Asthma" && !bunker.hasVentilation) score -= 15;
            if (p->health == "Immune weak" && bunker.medsKits == 0) score -= 15;
        }

        // Phobia penalties
        for (auto *p : team) {
            if (p->phobia == "Claustrophobia") score -= 10;
            if (p->phobia == "Darkness" && !bunker.hasPower) score -= 10;
            if (p->phobia == "Fire" && bunker.hasPower) score -= 5; // wires, generator etc.
        }

        // Items bonuses
        for (auto *p : team) {
            string it = p->item;
            if (it == "First aid kit") score += 10;
            if (it == "Medicine bag") score += 10;
            if (it == "Water filter") score += 10;
            if (it == "Toolbox") score += 8;
            if (it == "Radio") score += 5;
            if (it == "Generator parts") score += 8;
            if (it == "Seeds") score += 6;
        }

        // Traits bonuses/penalties
        for (auto *p : team) {
            if (p->trait == "Leader") score += 5;
            if (p->trait == "Calm under pressure") score += 5;
            if (p->trait == "Team player") score += 5;
            if (p->trait == "Aggressive") score -= 5;
            if (p->trait == "Panic prone") score -= 8;
            if (p->trait == "Selfish") score -= 5;
        }

        // clamp
        if (score < 0) score = 0;
        if (score > 150) score = 150;
        return score;
    }

    void finalResult() {
        cout << "\n=== FINAL TEAM IN BUNKER ===\n";
        vector<Player*> team;
        for (auto &p : players) if (p.alive) team.push_back(&p);

        for (auto *p : team) {
            cout << p->name
                 << " | Job: " << p->job
                 << " | Age: " << p->age
                 << " | Health: " << p->health
                 << " | Hobby: " << p->hobby
                 << " | Phobia: " << p->phobia
                 << " | Item: " << p->item
                 << " | Trait: " << p->trait
                 << "\n";
        }

        int score = computeScore(team);

        cout << "\n--- SURVIVAL CHECK ---\n";
        cout << "Score: " << score << " / 150\n";
        if (score >= 90) cout << "RESULT: SURVIVED ✅\n";
        else cout << "RESULT: FAILED ❌\n";

        cout << "\n(Quick hint) If you want harder: increase requiredSkills or raise min resources.\n";
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    cout << std::unitbuf;

    Game g;
    g.run();
    return 0;
}
