#ifndef SRC_CONSTANTS_TEXTS_H_
#define SRC_CONSTANTS_TEXTS_H_

#include <vector>
#include <string>

namespace texts {

  typedef std::vector<std::vector<std::string>> paragraphs_t;
  const paragraphs_t welcome = {
    {
      "Welcome to DISSONANCE"
    },
    {
      "Each player starts with a nucleus.",
      "One nucleus has control over a few cells surrounding it.",
      "By gathering different resources (iron, oxygen, potassium, ...) you can create Synapses to generate potential, advancing towards the enemies nucleus.",
      "When a certain amount of potential has reached the enemies nucleus, your enemy is destroyed.",
      "By activating cells you control, these cells can neutralize incoming potential."
    },
    {
      "You randomly gain iron every few seconds (the more the game advances the less iron you gain).",
      "Iron can be used to activate the process of gathering new resources or to boost your oxygen production.",
      "Depending on your current oxygen level, you gain more or less resources.",
      "Oxygen is also needed to create Synapses or to activate sells for your defences. But be careful:",
      "the more oxygen you spend on building advanced neurons (Synapses/ activated neurons) the less resources you gain per seconds!"
    },
    {
      "Once you started gaining dopamine and serotonin, you can develop advanced technologies, allowing you f.e. to target specific enemy neurons and hence destroy enemy synapses or activated neurons.",
      "Other technologies or advanced use of potentials are waiting for you...\n\n"
    },
    {
      "When dissonance starts, remember you should boast oxygen and activate production of glutamate, to start defending yourself.",
      "Also keep in mind, that there are two kinds of potential: ",
      "EPSP (strong in attack) and IPSP (blocks buildings); you should start with EPSP."
    }
  };

  const paragraphs_t help = {
    {
      "##### HELP #####",
      "",
      "--- COSTS (Potential/ Neurons) ----", 
      "ACTIVATE NEURON: oxygen=8.9, glutamate=19.1",
      "SYNAPSE: oxygen=13.4, potassium=6.6",
      "EPSP: potassium=4.4",
      "IPSP: potassium=3.4, chloride=6.8",
    },
    {
      "##### HELP #####",
      "",
      "--- COSTS (Potential/ Neurons) ----", 
      "WAY (select way/ way-points for neurosn): dopamine=7.7",
      "SWARM (launch swarm attacks +3): dopamine=9.9",
      "TARGET (choose target: ipsp/ epsp): dopamine=6.5",
      "TOTAL OXYGEN (max allowed oxygen bound+free): dopamine=7.5, serotonin=8.9",
      "TOTAL RESOURCE (max allowed resources: each): dopamine=8.5, serotonin=7.9",
      "CURVE (resource curve slowdown): dopamine=11.0, serotonin=11.2",
      "POTENIAL (increases potential of ipsp/ epsp): dopamine=5.0, serotonin=11.2",
      "SPEED (increases speed of ipsp/ epsp): dopamine=3.0, serotonin=11.2",
      "DURATION (increases duration at target of ipsp): dopamine=2.5, serotonin=11.2)",
    },
    {
      "##### HELP #####",
      "",
      "--- TIPS ----", 
      "Iron is used to boast oxygen production (1 iron per boast) or to start gaining new resources (2 iron per new resource).",
      "Iron is gained in relation to you oxygen-level: you only gain iron if oxygen is below 10 and you may never have more than 3 iron at a time!",
      "",
      "You should start investing into activate neurons to defend yourself: for this you need oxygen and glutamate.",
      "",
      "To start building units, you first need to build a synapse.",
      "EPSP aims to destroy enemy buildings, while IPSP blocks buildings.",
      "",
      "Also remember you gain resources from FREE oxygen. The more bound oxygen you have, then less resources you gain!"
    }
  };
}

#endif