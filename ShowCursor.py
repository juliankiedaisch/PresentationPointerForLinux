from Xlib import display
from tkinter import *
import threading
import time
import queue

class myThread (threading.Thread):
    def __init__(self, window, queue):
        threading.Thread.__init__(self)
        self.window = window
        self.display = display.Display()
        self.queue = queue
    def run(self):
        message = None
        while True:
            try:
                message = self.queue.get(timeout=0.01)
            except queue.Empty:
                pass
            if message:
                break
            coord = self.display.screen().root.query_pointer()._data
            print(str(coord["root_x"]) + ":" + str(coord["root_y"]))
            self.window.geometry("+" + str(coord["root_x"]+10) + "+" + str(coord["root_y"]+10))
            time.sleep(0.01)


#Create an instance of tkinter window or frame
win= Tk()
queue1 = queue.Queue()
#Setting the geometry of window
win.geometry("30x30")
win.overrideredirect(True)
win.bind('a', lambda event: queue1.put("stop"))
win.lift()
#Make the window jump above all
win.attributes('-topmost',True)
win.attributes("-alpha", 0.3)
win.configure(bg='#ffa348')
thread1 = myThread(win, queue1)
thread1.start()


win.mainloop()
queue1.put("stop")
thread1.join()