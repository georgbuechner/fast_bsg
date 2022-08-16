#ifndef SRC_PLAYER_AUDIOKI_H_
#define SRC_PLAYER_AUDIOKI_H_

#include <cstddef>
#include <deque>
#include <queue>
#include <shared_mutex>
#include <vector>

#include "share/audio/audio.h"
#include "server/game/field/field.h"
#include "share/defines.h"
#include "share/objects/units.h"
#include "server/game/player/player.h"

class AudioKi : public Player {
  public:
    AudioKi(position_t nucleus_pos, Field* field, Audio* audio, RandomGenerator* ran_gen, int color);
    ~AudioKi() {};

    // getter 
    std::deque<AudioDataTimePoint> data_per_beat() const;
    std::map<std::string, size_t> strategies() const;
    
    void SetUpTactics(bool economy_tactics);
    bool DoAction();
    void HandleIron(const AudioDataTimePoint& data_at_beat);

  private:
    // members
    Audio* audio_;
    std::deque<AudioDataTimePoint> data_per_beat_;
    const float average_level_;
    size_t max_activated_neurons_;
    position_t nucleus_pos_;

    AudioDataTimePoint last_data_point_;
    Interval cur_interval_;
    std::vector<AudioDataTimePoint> last_data_points_above_average_level_;

    std::map<size_t, size_t> epsp_target_strategies_;  // DESTROY nucleus/activatedneurons/synapses/resources
    std::map<size_t, size_t> ipsp_target_strategies_;  // BLOCK activatedneurons/synapses/resources
    std::map<size_t, size_t> ipsp_epsp_strategies_; // front-/ surround-focus
    std::map<size_t, size_t> activated_neuron_strategies_; // front-/ surround-focus
    std::map<size_t, size_t> def_strategies_; // front-/ surround-focus
                                                      
    std::vector<size_t> resource_tactics_;
    std::set<size_t> resources_activated_;
    std::vector<size_t> technology_tactics_;
    std::map<size_t, size_t> building_tactics_;

    // functions 
    bool DoAction(const AudioDataTimePoint& data_at_beat);

    void LaunchAttack(const AudioDataTimePoint& data_at_beat);

    // Create potental/ neurons. Add technology
    void CreateEpsps(position_t synapse_pos, position_t target_pos, int num_epsps_to_create, int speed_decrease);
    void CreateIpsps(position_t synapse_pos, position_t target_pos, int num_ipsp_to_create);
    void CreateSynapses(bool force=false);
    void CreateActivatedNeuron(bool force=false);
    void NewTechnology(const AudioDataTimePoint& data_at_beat);


    size_t AvailibleIpsps();
    size_t AvailibleEpsps(size_t ipsps_to_create);
    std::vector<position_t> AvailibleIpspLaunches(std::vector<position_t>& synapses, int min);
    size_t GetLaunchAttack(const AudioDataTimePoint& data_at_beat, size_t ipsps_to_create);

    std::vector<position_t> GetEpspTargets(position_t synapse_pos, std::list<position_t> way, size_t ignore_strategy=-1);
    std::vector<position_t> GetIpspTargets(std::list<position_t> way, std::vector<position_t>& synapses, 
        size_t ignore_strategy=-1);

    // Other stretegies

    /**
     * Gets called to add defencive structures, if enemy has potentals.
     * Calls `IpspDef` or `CreateExtraActivatedNeurons`, depending on
     * strategies.
     */
    void Defend();

    /**
     * Launches Ipsp in the direction of enemy potentials.
     * @param[in] enemy_potentials (number of incoming enemy epsps)
     * @param[in] way (way of random enemy incoming epsp)
     * @return false if defence-strategy could not be applied, true otherwise.
     */
    bool IpspDef(unsigned int enemy_potentials, std::list<position_t> way, int diff);

    /**
     * Creates extra activated neuerons based on enemies attack-strength.
     * @param[in] enemy_potentials (number of incoming enemy epsps)
     * @param[in] way (way of random enemy incoming epsp)
     * @return false if defence-strategy could not be applied, true otherwise.
     */
    bool CreateExtraActivatedNeurons(unsigned int enemy_potentials, std::list<position_t> way, int diff);

    /**
     * Fixes high bounds on resources.
     * Finds resource with highest bound (>70), while prioritising oxygen (>65).
     * First solution: increase resource-bounds.
     * Second solution: distribute all iron but on to given resource.
     * Last solution: try to destroy synapse.
     */
    void HandleHighBound();

    // helpers
    typedef std::list<std::pair<size_t, size_t>> sorted_stragety; ///< value -> strategy
    sorted_stragety SortStrategy(std::map<size_t, size_t> strategy) const;
    size_t GetTopStrategy(std::map<size_t, size_t> strategy) const;
    std::vector<position_t> GetAllActivatedNeuronsOnWay(std::vector<position_t> neurons, std::list<position_t> way);
    std::vector<position_t> SortPositionsByDistance(position_t start, std::vector<position_t> positions, bool reverse=false);
    /**
     * Gets enemy neurons of given type by how strongly defended by activated
     * neurons.
     * Should be used on either SYNAPSE, or RESOURCENEURON.
     * @param[in] start (position from which potential starts)
     * @param[in] neuron_type 
     */
    std::vector<position_t> GetEnemyNeuronsSortedByLeastDef(position_t start, int neuron_type);
    size_t GetMaxLevelExeedance() const;
    int SynchAttacks(size_t epsp_way_length, size_t ipsp_way_length);
    int GetVoltageOfAttackedNucleus(position_t enemy_target_pos);

    /**
     * Extraordinarily increases next resource, if iron is low, but an active resource is plentiful.
     * Deactives highest resource (>40) and uses iron, to active next resource. 
     * Put's deactivated resource as next in line. 
     * Does not do anything if next resource is also plentiful.
     * @return true took action, false otherwise.
     */
    bool LowIronResourceDistribution();
    std::vector<position_t> FindBestWayPoints(position_t synapse, position_t target);

    void SetBattleTactics();
    void SetEconomyTactics();
};

#endif
