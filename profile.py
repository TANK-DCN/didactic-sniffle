"""Learn to use cloudlab, and try to config 8 server for homa test."""

#
# NOTE: This code was machine converted. An actual human would not
#       write code like this!
#

# Import the Portal object.
import geni.portal as portal
# Import the ProtoGENI library.
import geni.rspec.pg as pg
# Import the Emulab specific extensions.
import geni.rspec.emulab as emulab

# Create a portal object,
pc = portal.Context()

# Create a Request object to start building the RSpec.
request = pc.makeRequestRSpec()

pc.defineParameter("server_num",
                   "server number",
                   portal.ParameterType.INTEGER, 1)
                   
pc.defineParameter("if_switch",
                   "if need switchr",
                   portal.ParameterType.BOOLEAN, False, [True, False])

pc.defineParameter("hardware_type",
                   "Optional physical node type (d710, c8220, etc)",
                   portal.ParameterType.STRING, "")

pc.defineParameter("phystype", "Switch type",
                   portal.ParameterType.STRING, "dell-s4048",
                   [('mlnx-sn2410', 'Mellanox SN2410'),
                    ('dell-s4048',  'Dell S4048')])

# Retrieve the values the user specifies during instantiation.
params = pc.bindParameters()

num = params.server_num

swifaces = []
lan = None
# Add Switch to the request and give it a couple of interfaces
if params.if_switch:
    mysw = request.Switch("mysw");
    mysw.hardware_type = params.phystype
    for i in range(num):
        swifaces.append(mysw.addInterface())
else:
    lan = request.LAN()

nodes = []
for i in range(num):
    node_name = "node" + str(i)
    node = request.RawPC(node_name)
    if params.hardware_type != "":
        node.hardware_type = params.hardware_type
    else:
        node.hardware_type = "d6515"

    node.installRootKeys(False, True)
    node.disk_image = "urn:publicid:IDN+emulab.net+image+emulab-ops:UBUNTU18-64-STD"
    iface = node.addInterface("eth1")
    # ip_addr = "192.168.1."+str(i+1)
    # iface.addAddress(pg.IPv4Address(ip_addr, "255.255.255.0"))
    
    if params.if_switch:
        link_name = "link"+str(i)
        link = request.L1Link(link_name)
        link.addInterface(iface)
        link.addInterface(swifaces[i])
    else:
        lan.addInterface(iface)
    


# Install and execute scripts on the node.
# node.addService(rspec.Install(url="http://example.org/sample.tar.gz", path="/local"))
# node.addService(rspec.Execute(shell="bash", command="/local/example.sh"))
node.addService(pg.Execute(shell="sh", command="/local/repository/install.sh"))


# Print the generated rspec
pc.printRequestRSpec(request)
