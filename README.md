# blockdag-sim

C++ simulations of two distributed BlockDAG algorithms:

1. **Hybrid tip selection (Tangle-like)**  
   - Multiple processes with local DAG views  
   - Delayed message passing  
   - Hybrid tip selection (random + MCMC)  
   - Logs tip statistics as a proxy for DAG width

2. **Witness-based bounded-width DAG**  
   - Multiple users, each with their own chain  
   - Blocks reference own parent + up to K witness blocks  
   - Logs global leaf count (width) over time

## Build

```bash
mkdir build
cd build
cmake ..
make

