from Xlib import display
from tkinter import *
import threading
import time
from tkinter.colorchooser import askcolor
from libinput import LibInput, constant, event


cursor = None
global win_open
win_open = False

class Cursor ():
    def __init__(self, root):
        self.__thread = None
        self.__root = root
        self.__status = False
        self.__window = None
        self.__display = display.Display()
        self.__color = '#ffa348'
        self.__size = 30
        self.__alpha = 0.6
        self.__intervall = 0.01
        self.__li = LibInput()
        self.__devices = [self.__li.path_add_device('/dev/input/event6'), self.__li.path_add_device('/dev/input/event7'), self.__li.path_add_device('/dev/input/event4')]
        
        self.__createWindow()

    def __createWindow(self):
        self.__destroyWindow()
        self.__window = Toplevel(self.__root)
        self.__window.attributes('-topmost',True)
        self.__window.overrideredirect(True)
        self.__updateWindow()
        self.__window.lift()
        self.__updateWindow()
        self.windowHide()

    def __destroyWindow(self):
        if self.__window:
            self.windowShow()
            try:
                self.__window.destroy()
            except:
                pass
            self.__window = None

    def __updateWindow(self):
        self.__window.geometry("{}x{}".format(self.__size, self.__size))
        self.__window.configure(bg=self.__color)
        self.__window.attributes("-alpha", self.__alpha)
        self.__window.update()

    def stop(self):
        self.__status = False
        if self.__thread:
            self.__thread.join()
        self.__thread = None

    def destroy(self):
        self.stop()
        for item in self.__devices:
            self.__li.path_remove_device(item)
        self.__destroyWindow()

    def start(self):
        if self.__thread:
            self.stop()
        self.__status = True
        self.__thread = threading.Thread(target=self.__run)
        self.__thread.start()

    def getColor(self):
        return self.__color

    def setColor(self, color):
        self.__color = color
        self.__updateWindow()

    def getAlpha(self):
        return self.__alpha
        
    def setAlpha(self, alpha):
        self.__alpha = alpha
        self.__updateWindow()

    def getSize(self):
        return self.__size
        
    def setSize(self, size):
        try:
            size = int(size)
        except:
            return
        self.__size = size
        self.__updateWindow()

    def getIntervall(self):
        return self.__intervall
        
    def setIntervall(self, intervall):
        self.__intervall = intervall

    def windowHide(self):
        self.__window.withdraw()
    def windowShow(self):
        self.__window.deiconify()

    def __run(self):
        message = None
        index = 0
        while self.__status:
            try:
                for xevent in self.__li.get_event(self.__intervall):
                    if xevent.type == constant.Event.TOUCH_MOTION:
                        tool = xevent.get_touch_event()
                        self.__window.geometry("+" + str(int(tool.get_x()*6.7)+10) + "+" + str(int(tool.get_y()*6.7)+10))

                    elif xevent.type == constant.Event.POINTER_MOTION_ABSOLUTE:
                        tool = xevent.get_pointer_event()
                        self.__window.geometry("+" + str(int(tool.get_x()*6.7)+10) + "+" + str(int(tool.get_y()*6.7)+10))

                    elif xevent.type == constant.Event.TABLET_TOOL_AXIS:
                        tool = xevent.get_tablet_tool_event()
                        self.__window.geometry("+" + str(int(tool.get_x()*6.7)+10) + "+" + str(int(tool.get_y()*6.7)+10))
                    elif xevent.type == constant.Event.TOUCH_DOWN:
                        self.windowShow()
                    elif xevent.type == constant.Event.TOUCH_UP:
                        self.windowHide()
                    elif xevent.type == constant.Event.TABLET_TOOL_PROXIMITY:
                        tool = xevent.get_tablet_tool_event()
                        if tool.get_proximity_state()==constant.TabletToolProximityState.IN:
                            self.windowShow()
                        else:
                            self.windowHide()
        
            except RuntimeError:
                pass

def windowClose():
    try:
        cursor.destroy()
        win2.destroy()
        win.destroy()
    except:
        pass

def changeSize(*args):
    if cursor:
        cursor.setSize(spinSize.get())
def changeAlpha(*args):
    if cursor:
        try:
            cursor.setAlpha(0.01*float(spinAlpha.get()))
        except ValueError:
            pass
def changeIntervall(*args):
    if cursor:
        try:
            cursor.setIntervall(1.0/float(spinIntervall.get()))
        except ValueError:
            pass
def changeColor():
    if cursor:
        colors = askcolor(title="Tkinter Color Chooser")
        cursor.setColor(colors[1])
        win2.configure(bg=colors[1])



#Create an instance of tkinter window or frame
win= Tk()

DEFAULT_SIZE = StringVar(win)
DEFAULT_SIZE.set(30)
DEFAULT_SIZE.trace("w", changeSize)
DEFAULT_ALPHA = StringVar(win)
DEFAULT_ALPHA.set(70)
DEFAULT_ALPHA.trace("w", changeAlpha)
DEFAULT_INTERVALL = StringVar(win)
DEFAULT_INTERVALL.set(100)
DEFAULT_INTERVALL.trace("w", changeIntervall)

#win.attributes('-topmost',True)

win.geometry("280x180")
win.resizable(False, False)
win.title("Show Cursor Settings")

win.columnconfigure(0, weight=3)
win.columnconfigure(1, weight=1)

#Size
spinSize = Spinbox(win, from_=10, to=100, width=10, textvariable=DEFAULT_SIZE)
spinSize.grid(column=1, row=0, sticky=W, padx=5, pady=5)
labelSize = Label(win, text="Cursor Size: ").grid(column=0, row=0, sticky=E, padx=5, pady=5)
#labelSize.pack(side=LEFT)
#spinSize.pack(side=RIGHT)

#Alpha
spinAlpha = Spinbox(win, from_=10, to=100, width=10, textvariable=DEFAULT_ALPHA)
spinAlpha.grid(column=1, row=1, sticky=W, padx=5, pady=5)
labelAlpha = Label(win, text="Cursor Alpha (percent): ").grid(column=0, row=1, sticky=E, padx=5, pady=5)


#Intervall
spinIntervall= Spinbox(win, from_=1, to=100, width=10, textvariable=DEFAULT_INTERVALL)
spinIntervall.grid(column=1, row=2, sticky=W, padx=5, pady=5)
labelIntervall = Label(win, text="Cursorupdates per second: ").grid(column=0, row=2, sticky=E, padx=5, pady=5)

#Color
buttonColor = Button(win, text='Select', command=changeColor).grid(column=1, row=3, sticky=W, padx=5, pady=5)
labelColor = Label(win, text="Choose Color for Cursor: ").grid(column=0, row=3, sticky=E, padx=5, pady=5)

#Quit
buttonQuit = Button(win, text='Quit', command=windowClose, bg="red").grid(column=0, row=5, sticky=E, padx=5, pady=5)


def click_handler(*args):
    global win_open
    if win_open:
        win.withdraw()
        win_open = False
    else:
        win.deiconify()
        win_open = True
    


#Setting Activation Window
win2 = Toplevel(win)
win2.geometry("50x30")
win2.geometry("+0+0")
win2.resizable(False, False)
win2.attributes('-topmost',True)
win2.overrideredirect(True)
win2.configure(bg="#ffa348")

win2.bind("<Button>", click_handler)

#Setting the geometry of window
cursor = Cursor(win)
cursor.start()

win.protocol("WM_DELETE_WINDOW", click_handler)
win.withdraw()
win.mainloop()

cursor.destroy()
