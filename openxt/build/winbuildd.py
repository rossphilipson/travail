#!/usr/bin/env python

# Some idiot interpreters don't always set up __file__
# (I'm looking at you PythonWin)
try:
    __file__
except NameError:
    import sys
    __file__ = sys.argv[0]
    del sys

import SimpleXMLRPCServer
import os
import subprocess as subprocess_
import socket
import time
import sys
import getopt
import shutil
import ConfigParser

subprocess = subprocess_
linesep = '\n'
SCRIPTDIR = os.path.dirname(__file__)

def write_(*args):
  """Place-holder for print"""
  if args:
    print ' '.join(map(str, args))
  else:
    print
write = write_

def onerror(func, path, exc_info):
    """
    Error handler for ``shutil.rmtree``.

    If the error is due to an access error (read only file)
    it attempts to add write permission and then retries.

    If the error is for another reason it re-raises the error.

    Usage : ``shutil.rmtree(path, onerror=onerror)``
    """
    import stat
    if not os.access(path, os.W_OK):
        # Is the error an access error ?
        os.chmod(path, stat.S_IWUSR)
        func(path)
    else:
        raise

class PopenWrapper(object):
  """Like popen but actually writes stuff out to a log file too."""
  class WaitWrapper(object):
    """Wraps the wait object."""
    def __init__(self, waiter, log, cmdline):
        super(PopenWrapper.WaitWrapper, self).__init__()
        self.waiter = waiter
        self.log = log
        self.cmdline = cmdline
    def __getattr__(self, name):
        return getattr(self.waiter, name)
    def wait(self, *args, **kwargs):
        cmdline = self.cmdline
        self.log.write(linesep + 'EXECUTING: ' + cmdline)
        start = time.time()
        result = self.waiter.wait(*args, **kwargs)
        end = time.time()
        self.log.write(('FINISHED EXECUTING (%.02f' % (end - start)) + '): ' + cmdline + linesep)
        return result

  def __init__(self, logfile):
    """Initialise this PopenWrapper instance.
        Parameters:
          logfile  - File to write any output to.
    """
    super(PopenWrapper, self).__init__()
    self.logfile = logfile
    
  def Popen(self, *unargs, **kwargs):
    """Wrapper around the real popen. Dump the command line...
        Parameters:
          cmdline  - Command line being executed.
    """
    cmdline = None
    if 'args' in kwargs:
        cmdline = kwargs['args']
    else:
      cmdline = unargs[0]
    return PopenWrapper.WaitWrapper(subprocess_.Popen(*unargs, **kwargs), self, cmdline)

  def write(self, *args):
    """Placeholder for print"""
    write_(*args) #Print in build server.
    if args:
        self.logfile.write(' '.join(map(str, args)) + linesep) #Print to log file.
        self.logfile.flush()
    else:
        self.logfile.write(linesep)
        self.logfile.flush()

class FakeLog(object):
    """Used when log hasn't been created."""
    def close(self):
        """Place-holder close command."""
        pass

class RPCInterface(object):
    def make(self,build='',branch='master',certname='',developer='false'):
        """make(build,branch,certname,developer) - Call powershell scripts to do the real work - Ex: make("123456","master","developer","false")"""
        log = FakeLog()

        # Create log directory/file
        try:
            os.chdir(BUILDDIR)
            log = open('output.log',"w")
            print "Log file created @ " + socket.gethostname() + " file: " + BUILDDIR + "\\output.log"
            subprocess = PopenWrapper(log)
            write = subprocess.write
        except:
            raise Exception, "ERROR: Unable to chdir directory " + BUILDDIR + " or open log"

        write("Start build, RPC input:")
        write('make(build='+repr(build)+',branch='+repr(branch)+\
              ',certname='+repr(certname)+'developer='+repr(developer)+')')
        write('Running in dir: ' + os.getcwd())

        # Nuke existing build
        if os.path.exists(BUILDDIR + '\\openxt'):
            shutil.rmtree(BUILDDIR + '\\openxt', onerror=onerror)

        # Clone the main OpenXT repo and checkout branch
        subprocess.Popen('git clone '+ GITURL + '/openxt.git', shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        write("Completed cloning " + GITURL + "/openxt.git")
        os.chdir(BUILDDIR + "\\openxt")
        subprocess.Popen('git checkout -b '+ branch, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        write('Checked out '+ branch +' in openxt.git')
        os.chdir(BUILDDIR + "\\openxt\\windows")

        command = 'sed -i "s/Put Your Company Name Here/OpenXT/g" config\\sample-config.xml'
        subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()

        # TODO example using sed
        #command = 'sed -e "s/!define CurrentMajorVersion.*$/!define CurrentMajorVersion '+str(major)+'/g" xensetup.nsi > temp.nsi && move /Y temp.nsi xensetup.nsi'
        #subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()

        # TODO example using some app and change dir 
        #subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        #command = 'makensis /DINSTALL_XENVESA /DINSTALL_XENVESA8 xensetup.nsi'
        #subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        #os.chdir(BUILDDIR + uid)

        # TODO example using powershell 
        #Build win-tools
        #write("Building win-tools...")
        #command = 'powershell win-tools\\do_build.ps1 -c ' + config 
        #subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()

        # TODO example test path if else exception
        #if os.path.exists('msi-installer\\iso\\Packages\\XenClientTools.msi') and os.path.exists('msi-installer\\iso\\Packages\\XenClientTools64.msi'):
            # Do stuffs
        #else:
            # Do other stuffs
            #raise Exception, "ERROR: XenClientTools failed to build. See \\\\" + socket.gethostname() + "\\" + uid + "\\output.log for details."

        log.close()
        return 'SUCCESS'

    def hello(self):
        return "hello back"

    def status(self):
        pass

    def retrieve_file(self, filename):
        pass

def main(argv):
	
	config = ""
	site = ""
	try:
		opts, args = getopt.getopt(argv, "hc:s:", ["help", "config=", "site="])
		for opt, arg in opts:
			if opt in ("-h", "--help"):
				usage()
				sys.exit()
			elif opt in ("-c", "--config"): 
				config = arg
			elif opt in ("-s", "--site"):
				site = arg
	except getopt.GetoptError:
		usage()
		sys.exit(2)
		
	loadConfig(config,site)
	s = SimpleXMLRPCServer.SimpleXMLRPCServer(('', PORT))
	s.register_introspection_functions()
	s.register_instance(RPCInterface())

	try:
		print """
		 OpenXT Windows Build XMLRPC Server
	 
		 Use Control-C to exit
		 """
		s.serve_forever()
	except KeyboardInterrupt:
		print 'Exiting'
	
def loadConfig(cfg,site):
	try:
		config = ConfigParser.ConfigParser()
		config.read(cfg)
	except:
		print "Configuration file cannot be read."
		sys.exit()
	
	if not (config.has_section(site)):
		print "Invalid site specified. Available sites are:"
		print config.sections()
		sys.exit()
	else:
		try:
			global PORT, BUILDDIR, CONFIG, GITURL
			PORT = config.getint(site,'port')
			BUILDDIR = config.get(site,'builddir')
			CONFIG = config.get(site,'config')
			GITURL = config.get(site,'giturl')
		except:
			print "Exception getting configuration option. Corrupt .cfg file? Missing option?"
			sys.exit()				

def usage():
	print """
	 Usage:
	 > python builddaemon.py -c "config file" -s "site"
	"""

if __name__ == '__main__':
	main(sys.argv[1:])
	
