/*
 * The script builds a cactus tree representation of the chains and nets.
 * The format of the output graph is dot format.
 */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>

#include "cactus.h"
#include "avl.h"
#include "commonC.h"
#include "hashTableC.h"

#include "utilitiesShared.h"

/*
 * Global variables.
 */
static bool nameLabels = 0;

static void usage() {
	fprintf(stderr, "cactus_graphViewer, version 0.2\n");
	fprintf(stderr, "-a --logLevel : Set the log level\n");
	fprintf(stderr, "-c --netDisk : The location of the net disk directory\n");
	fprintf(stderr, "-d --netName : The name of the net (the key in the database)\n");
	fprintf(stderr, "-e --outputFile : The file to write the dot graph file in.\n");
	fprintf(stderr, "-g --nameLabels : Give chain and net nodes name labels.\n");
	fprintf(stderr, "-h --help : Print this help screen\n");
}

void addEndNodeToGraph(End *end, FILE *fileHandle) {
	const char *nameString = netMisc_nameToStringStatic(end_getName(end));
	graphViz_addNodeToGraph(nameString, fileHandle, nameString, 0.5, 0.5, "circle", "black", 14);
}

void addEdgeToGraph(End *end1, End *end2, const char *colour, double length, double weight, FILE *fileHandle) {
	char *nameString1 = netMisc_nameToString(end_getName(end1));
	char *nameString2 = netMisc_nameToString(end_getName(end2));
	graphViz_addEdgeToGraph(nameString1, nameString2, fileHandle, "", colour, length, weight, "forward");
}

void addAtomToGraph(Atom *atom, const char *colour, FILE *fileHandle) {
	End *leftEnd = atom_getLeftEnd(atom);
	End *rightEnd = atom_getRightEnd(atom);
	addEndNodeToGraph(leftEnd, fileHandle);
	addEndNodeToGraph(rightEnd, fileHandle);
	Atom_InstanceIterator *iterator = atom_getInstanceIterator(atom);
	AtomInstance *atomInstance;
	while((atomInstance = atom_getNext(iterator)) != NULL) {
		assert(atomInstance != NULL);
		addEdgeToGraph(leftEnd, rightEnd, colour, 5, 10, fileHandle);
	}
	atom_destructInstanceIterator(iterator);
}

void addTrivialChainsToGraph(Net *net, FILE *fileHandle) {
	/*
	 * Add atoms not part of chain to the graph
	 */
	Net_AtomIterator *atomIterator = net_getAtomIterator(net);
	Atom *atom;
	while((atom = net_getNextAtom(atomIterator)) != NULL) {
		if(atom_getChain(atom) == NULL) {
			addAtomToGraph(atom, "black", fileHandle);
		}
	}
	net_destructAtomIterator(atomIterator);
}

void addChainsToGraph(Net *net, FILE *fileHandle) {
	/*
	 * Add atoms part of a chain to the graph.
	 */
	Net_ChainIterator *chainIterator = net_getChainIterator(net);
	Chain *chain;
	while((chain = net_getNextChain(chainIterator)) != NULL) {
		int32_t i;
		const char *chainColour = graphViz_getColour(chainColour);
		for(i=1; i<chain_getLength(chain); i++) {
			Link *link = chain_getLink(chain, i);
			Atom *atom = end_getAtom(link_getLeft(link));
			assert(atom != NULL);
			addAtomToGraph(atom, chainColour, fileHandle);
		}
	}
	net_destructChainIterator(chainIterator);
}

void addAdjacencies(Net *net, FILE *fileHandle) {
	/*
	 * Adds adjacency edges to the graph.
	 */
	Net_EndIterator *endIterator = net_getEndIterator(net);
	End *end;
	while((end = net_getNextEnd(endIterator)) != NULL) {
		End_InstanceIterator *instanceIterator = end_getInstanceIterator(end);
		EndInstance *endInstance;
		while((endInstance = end_getNext(instanceIterator)) != NULL) {
			EndInstance *endInstance2 = endInstance_getAdjacency(endInstance);
			addEdgeToGraph(endInstance_getEnd(endInstance), endInstance_getEnd(endInstance2), "grey", 10, 1, fileHandle);
		}
		end_destructInstanceIterator(instanceIterator);
	}
	net_destructEndIterator(endIterator);
}

void addStubAndCapEndsToGraph(Net *net, FILE *fileHandle) {
	Net_EndIterator *endIterator = net_getEndIterator(net);
	End *end;
	while((end = net_getNextEnd(endIterator)) != NULL) {
		if(!end_isAtomEnd(end)) {
			addEndNodeToGraph(end, fileHandle);
		}
	}
	net_destructEndIterator(endIterator);
}

void makeCactusGraph(Net *net, FILE *fileHandle) {
	if(net_getParentAdjacencyComponent(net) == NULL) {
		addStubAndCapEndsToGraph(net, fileHandle);
	}
	addTrivialChainsToGraph(net, fileHandle);
	addChainsToGraph(net, fileHandle);
	if(net_getAdjacencyComponentNumber(net) == 0) {
		addAdjacencies(net, fileHandle);
	}
	Net_AdjacencyComponentIterator *adjacencyComponentIterator = net_getAdjacencyComponentIterator(net);
	AdjacencyComponent *adjacencyComponent;
	while((adjacencyComponent = net_getNextAdjacencyComponent(adjacencyComponentIterator)) != NULL) {
		makeCactusGraph(adjacencyComponent_getNestedNet(adjacencyComponent), fileHandle);
	}
	net_destructAdjacencyComponentIterator(adjacencyComponentIterator);
}

int main(int argc, char *argv[]) {
	NetDisk *netDisk;
	Net *net;
	FILE *fileHandle;

	/*
	 * Arguments/options
	 */
	char * logLevelString = NULL;
	char * netDiskName = NULL;
	char * netName = NULL;
	char * outputFile = NULL;

	///////////////////////////////////////////////////////////////////////////
	// (0) Parse the inputs handed by genomeCactus.py / setup stuff.
	///////////////////////////////////////////////////////////////////////////

	while(1) {
		static struct option long_options[] = {
			{ "logLevel", required_argument, 0, 'a' },
			{ "netDisk", required_argument, 0, 'c' },
			{ "netName", required_argument, 0, 'd' },
			{ "outputFile", required_argument, 0, 'e' },
			{ "nameLabels", no_argument, 0, 'g' },
			{ "help", no_argument, 0, 'h' },
			{ 0, 0, 0, 0 }
		};

		int option_index = 0;

		int key = getopt_long(argc, argv, "a:c:d:e:gh", long_options, &option_index);

		if(key == -1) {
			break;
		}

		switch(key) {
			case 'a':
				logLevelString = stringCopy(optarg);
				break;
			case 'c':
				netDiskName = stringCopy(optarg);
				break;
			case 'd':
				netName = stringCopy(optarg);
				break;
			case 'e':
				outputFile = stringCopy(optarg);
				break;
			case 'g':
				nameLabels = !nameLabels;
				break;
			case 'h':
				usage();
				return 0;
			default:
				usage();
				return 1;
		}
	}

	///////////////////////////////////////////////////////////////////////////
	// (0) Check the inputs.
	///////////////////////////////////////////////////////////////////////////

	assert(logLevelString == NULL || strcmp(logLevelString, "INFO") == 0 || strcmp(logLevelString, "DEBUG") == 0);
	assert(netDiskName != NULL);
	assert(netName != NULL);
	assert(outputFile != NULL);

	//////////////////////////////////////////////
	//Set up logging
	//////////////////////////////////////////////

	if(logLevelString != NULL && strcmp(logLevelString, "INFO") == 0) {
		setLogLevel(LOGGING_INFO);
	}
	if(logLevelString != NULL && strcmp(logLevelString, "DEBUG") == 0) {
		setLogLevel(LOGGING_DEBUG);
	}

	//////////////////////////////////////////////
	//Log (some of) the inputs
	//////////////////////////////////////////////

	logInfo("Net disk name : %s\n", netDiskName);
	logInfo("Net name : %s\n", netName);
	logInfo("Output graph file : %s\n", outputFile);

	//////////////////////////////////////////////
	//Load the database
	//////////////////////////////////////////////

	netDisk = netDisk_construct(netDiskName);
	logInfo("Set up the net disk\n");

	///////////////////////////////////////////////////////////////////////////
	// Parse the basic reconstruction problem
	///////////////////////////////////////////////////////////////////////////

	net = netDisk_getNet(netDisk, netMisc_stringToName(netName));
	logInfo("Parsed the top level net of the cactus tree to build\n");

	///////////////////////////////////////////////////////////////////////////
	// Build the graph.
	///////////////////////////////////////////////////////////////////////////

	fileHandle = fopen(outputFile, "w");
	graphViz_setupGraphFile(fileHandle);
	makeCactusGraph(net, fileHandle);
	graphViz_finishGraphFile(fileHandle);
	fclose(fileHandle);
	logInfo("Written the tree to file\n");

	///////////////////////////////////////////////////////////////////////////
	// Clean up.
	///////////////////////////////////////////////////////////////////////////

	netDisk_destruct(netDisk);

	return 0;
}
