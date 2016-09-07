'''
    Sarosh Ahmad
    sahmad13

    MP4 - CSMA Simulation
    csma.py
    ECE 438 - FA 15

    usage: csma.py <inputfile>
    output: output.txt
'''


import sys, random

class Node:
    R = 8
    collisionCount = 0
    attemptCount = 0
    sentPacketsCount = 0
    sendingData = False
    backoff = 0

    def __init__ (self, number):
        self.number = number

    def sendData(self):
        self.sendingData = True

    def collisionOccured(self):
        self.collisionCount += 1
        self.attemptCount += 1

    def packetSent(self):
        self.sendingData = False
        self.sentPacketsCount += 1
        self.attemptCount = 0

    def packetDropped(self):
        self.attemptCount = 0


def main(argv):

    maxNodes = 25
    packetSize = 20
    maxAttempt = 6
    simulationTime = 50000
    totalCollisionCount = 0
    channelOccupied = False
    sendProgress = 0
    packetsDropped = 0
    packetsTransmitted = 0
    currentlySendingNode = -1

    # Variables used for experiment statistics
    timeIdle = 0
    timeSending = 0
    timeColliding = 0


    # Open input file
    inputfile = ''
    
    if len(sys.argv) != 2:
        print 'usage: csma.py <inputfile>'
        sys.exit()

    else:
        inputfile = sys.argv[1]

    # Parse inputs for simulation

    randnumRange = []
    f = open(inputfile)
    for line in f:
        var = line.split(" ")[0]
        if var == 'N':
            maxNodes = int( line.split(" ")[1] )

        elif var == 'L':
            packetSize = int( line.split(" ")[1] )

        elif var == 'R':
            randnumRange = line.split(" ")[1:]

        elif var == 'M':
            maxAttempt = int( line.split(" ")[1] )

        elif var == 'T':
            simulationTime = int( line.split(" ")[1] )

        else :
            print 'Bad input file!'
            sys.exit()

    f.close()
    # Create nodes
    nodes = []
    for i in range(maxNodes):
        n = Node(i)
        n.R = int(randnumRange[0])
        n.backoff = random.randint(0, n.R)
        nodes.append(n)

    # Run Simulation

    for t in range(simulationTime):
        if channelOccupied:
            timeSending += 1
            aboutToOccupy = False
            if sendProgress > 0:
                sendProgress -= 1

            if sendProgress == 0:
                channelOccupied = False
                nodes[currentlySendingNode].packetSent();
                nodes[currentlySendingNode].R = int(randnumRange[0])
                nodes[currentlySendingNode].backoff = random.randint(0, nodes[currentlySendingNode].R)
                packetsTransmitted += 1

        else:
            # Decerement all nodes' backoff
            sendingNodes = []
            for i in range(maxNodes):
                if nodes[i].backoff == 0:
                    sendingNodes.append(i)
                else:
                    nodes[i].backoff -= 1


            # More than one node is attempting to send
            if len(sendingNodes) > 1:
                # If nodes have collided, inform each node and reset backoff or drop pkt
                for i in sendingNodes:
                    nodes[i].collisionOccured()
                    totalCollisionCount += 1
                    if nodes[i].attemptCount < maxAttempt:
                        if nodes[i].attemptCount < len(randnumRange):
                            nodes[i].R = int(randnumRange[nodes[i].attemptCount]) 
                        else:
                            nodes[i].R *= 2

                    else:
                        nodes[i].packetDropped()
                        packetsDropped += 1
                        nodes[i].R = int(randnumRange[0])                          
                    nodes[i].backoff = random.randint(0, nodes[i].R)

                # Undo backoff decrement in nodes that aren't colliding
                for i in range(maxNodes):
                    if i not in sendingNodes:
                        nodes[i].backoff += 1
                timeColliding += 1

            elif len(sendingNodes) == 1:
                # Node in sendingNodes begins sending
                currentlySendingNode = sendingNodes[0]
                nodes[currentlySendingNode].sendData()
                channelOccupied = True
                sendProgress = packetSize
                timeSending += 1

                # Undo backoff decrement in nodes that aren't sending
                for i in range(maxNodes):
                    if i != sendingNodes[0]:
                        nodes[i]. backoff += 1

            else:
                # Nothing happened, continue backoff
                timeIdle += 1


    print timeColliding + timeIdle + timeSending == simulationTime
    print 'Simulation Complete'
    print 'Number of Nodes: ' + str(maxNodes)
    print ''
    print 'Packets Sent: ' + str(packetsTransmitted)
    print 'Packets Dropped: ' + str(packetsDropped)
    print 'Total Collisions: ' + str(totalCollisionCount)
    print 'Time Channel Busy: ' + str(timeSending)
    print 'Time Spent Idle: '   +  str(timeIdle)
    print 'Time Spent in Collision: ' + str(timeColliding)
    print 'Channel Idle Fraction: ' + str(float(timeIdle)/float(simulationTime))
    print 'Channel Utilization : ' + str(float(timeSending)/float(simulationTime))

    print '\n'

    # Calculate variances and write output to output.txt
    transSum = 0
    collSum = 0
    transNodes = []
    collNodes = []
    for i in range(maxNodes):
        transSum += nodes[i].sentPacketsCount
        transNodes.append(nodes[i].sentPacketsCount)
        collSum += nodes[i].collisionCount
        collNodes.append(nodes[i].collisionCount)

    transMean = transSum/maxNodes
    collMean = collSum/maxNodes
    transSqDiffSum = 0
    collSqDiffSum = 0
    for i in range(maxNodes):
        transSqDiffSum += pow(transMean-transNodes[i], 2)
        collSqDiffSum += pow(collMean-collNodes[i], 2)

    transVariance = float(transSqDiffSum)/float(maxNodes)
    collVariance = float(collSqDiffSum)/float(maxNodes)

    utilPercent = float(timeSending)/float(simulationTime) * 100.00
    idlePercent = float(timeIdle)/float(simulationTime) * 100.00

    f = open('output.txt', 'w')
    f.write('Channel utilization: ' + str(utilPercent) + '%\n')
    f.write('Channel idle fraction: ' + str(idlePercent) + '%\n')
    f.write('Total number of collisions: ' + str(totalCollisionCount) + '\n')
    f.write('Variance in number of successful transmissions across all nodes: ' + str(transVariance) + '\n')
    f.write('Variance in number of collisions across all nodes: ' + str(collVariance) + '\n')
    f.close()

    # All said and done


# Call to main

if __name__ == '__main__':
    main(sys.argv[1:])