#ifndef _DG_SLICING_H_
#define _DG_SLICING_H_

#include <set>

#include "NodesWalk.h"
#include "BFS.h"
#include "ADT/Queue.h"
#include "DependenceGraph.h"

#ifdef ENABLE_CFG
#include "BBlock.h"
#endif

using dg::ADT::QueueFIFO;

namespace dg {
namespace analysis {

// this class will go through the nodes
// and will mark the ones that should be in the slice
template <typename NodeT>
class WalkAndMark : public NodesWalk<NodeT, QueueFIFO<NodeT *>>
{
public:
    WalkAndMark(uint32_t flags)
        : NodesWalk<NodeT, QueueFIFO<NodeT *>>(flags) {}

    void mark(NodeT *start, uint32_t slice_id, bool with_entry = true)
    {
        WalkData data(slice_id, this);
        if (with_entry)
            this->walk(start, markSliceWithEntry, &data);
        else
            this->walk(start, markSlice, &data);
    }

private:
    struct WalkData
    {
        WalkData(uint32_t si, WalkAndMark *wm)
            : slice_id(si), analysis(wm) {}

        uint32_t slice_id;
        WalkAndMark *analysis;
    };

    static void markSlice(NodeT *n, WalkData *data)
    {
        uint32_t slice_id = data->slice_id;
        n->setSlice(slice_id);

#ifdef ENABLE_CFG
        // when we marked a node, we need to mark even
        // the basic block - if there are basic blocks
        BBlock<NodeT> *B = n->getBBlock();
        if (B)
            B->setSlice(slice_id);
#endif

        // the same with dependence graph, if we keep a node from
        // a dependence graph, we need to keep the dependence graph
        DependenceGraph<NodeT> *dg = n->getDG();
        if (dg)
            dg->setSlice(slice_id);
    }

    static void markSliceWithEntry(NodeT *n, WalkData *data)
    {
        markSlice(n, data);

        DependenceGraph<NodeT> *dg = n->getDG();
        if (dg) {
            // keep also all call-sites of this func (they are
            // control dependent on the entry node)
            // This is correct but not so precise - fix it later.
            // Now I need the correctness...
            NodeT *entry = dg->getEntry();
            assert(entry && "No entry node in dg");
            data->analysis->enqueue(entry);
        }
    }
};

struct SlicerStatistics
{
    SlicerStatistics()
        : nodesTotal(0), nodesRemoved(0), blocksRemoved(0) {}

    // total number of nodes that were checked for removing
    uint64_t nodesTotal;
    // total number of nodes actually removed (including the
    // ones removed in blocks)
    uint64_t nodesRemoved;
    // number of whole blocks removed
    uint32_t blocksRemoved;
};

template <typename NodeT>
class Slicer : Analysis<NodeT>
{
    uint32_t options;
    uint32_t slice_id;

    void sliceGraph(DependenceGraph<NodeT> *dg, uint32_t slice_id)
    {
        for (auto it : *dg) {
            NodeT *n = it.second;

            // slice subgraphs if this node is a call-site
            for (DependenceGraph<NodeT> *sub : n->getSubgraphs())
                sliceGraph(sub, slice_id);

            if (n->getSlice() != slice_id) {
                // do graph specific logic
                if (removeNode(n))
                    dg->deleteNode(n);
            }
        }

        // FIXME if graph own global nodes, slice the global nodes
    }

protected:

    // how many nodes and blocks were removed or kept
    SlicerStatistics statistics;

public:
    Slicer<NodeT>(uint32_t opt = 0)
        :options(opt), slice_id(0) {}

    SlicerStatistics& getStatistics() { return statistics; }
    const SlicerStatistics& getStatistics() const { return statistics; }

    uint32_t mark(NodeT *start, uint32_t sl_id = 0,
                  uint32_t flags = NODES_WALK_REV_CD |
                                   NODES_WALK_REV_DD |
                                   NODES_WALK_BB_POSTDOM_FRONTIERS,
                  bool withEntry = true)
    {
        if (sl_id == 0)
            sl_id = ++slice_id;

        WalkAndMark<NodeT> wm(flags);
        wm.mark(start, sl_id, withEntry);

        return sl_id;
    }

    uint32_t slice(NodeT *start, uint32_t sl_id = 0)
    {
        // for now it will does the same as mark,
        // just remove the rest of nodes
        sl_id = mark(start, sl_id);
        sliceGraph(start->getDG(), sl_id);

        return sl_id;
    }

    // remove node from the graph
    // This virtual method allows to taky an action
    // when node is being removed from the graph. It can also
    // disallow removing this node by returning false
    virtual bool removeNode(NodeT *node)
    {
        (void) node;
        return true;
    }

#ifdef ENABLE_CFG
    virtual void removeBlock(BBlock<NodeT> *block)
    {
        (void) block;
    }

    struct RemoveBlockData {
        uint32_t sl_id;
        std::set<BBlock<NodeT> *>& blocks;
    };

    static void getBlocksToRemove(BBlock<NodeT> *BB, RemoveBlockData& data)
    {
        if (BB->getSlice() == data.sl_id)
            return;

        data.blocks.insert(BB);
    }

    void sliceBBlocks(BBlock<NodeT> *start, uint32_t sl_id)
    {
        // we must queue the blocks ourselves before we potentially remove them
        BBlockBFS<NodeT> bfs(BFS_BB_CFG);
        std::set<BBlock<NodeT> *> blocks;

        RemoveBlockData data = { sl_id, blocks };
        bfs.run(start, getBlocksToRemove, data);

        for (BBlock<NodeT> *blk : blocks) {
            // update statistics
            statistics.nodesRemoved += blk->size();
            statistics.nodesTotal += blk->size();
            ++statistics.blocksRemoved;

            // call specific handlers (overriden by child class)
            removeBlock(blk);

            // remove block from the graph
            blk->remove();
        }
    }

    // remove BBlocks that contain no node that should be in
    // sliced graph
    void sliceBBlocks(DependenceGraph<NodeT> *graph, uint32_t sl_id)
    {
        typename DependenceGraph<NodeT>::BBlocksMapT& CB = graph->getBlocks();
#ifdef DEBUG_ENABLED
        uint32_t blocksNum = CB.size();
#endif
        // gather the blocks
        // FIXME: we don't need two loops, just go carefully
        // through the constructed blocks (keep temporary always-valid iterator)
        std::set<BBlock<NodeT> *> blocks;
        for (auto it : CB) {
            if (it.second->getSlice() != sl_id)
                blocks.insert(it.second);
        }

        for (BBlock<NodeT> *blk : blocks) {
            // update statistics
            statistics.nodesRemoved += blk->size();
            statistics.nodesTotal += blk->size();
            ++statistics.blocksRemoved;

            // call specific handlers (overriden by child class)
            removeBlock(blk);

            // remove block from the graph
            blk->remove();
        }

#ifdef DEBUG_ENABLED
        assert(CB.size() + blocks.size() == blocksNum &&
                "Inconsistency in sliced blocks");
#endif
    }

#endif
};

} // namespace analysis
} // namespace dg

#endif // _DG_SLICING_H_
