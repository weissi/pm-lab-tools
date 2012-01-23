#!/usr/bin/env python
import sys, pygame


white = 255,255,255
black = 0,0,0
red = 255,0,0
green = 0,255,0
blue = 0,0,255
purple = 255,0,255
orange = 255,128,0
yellow = 0,255,255
brown = 85, 58, 38

colors = [red,green,blue,purple,orange,yellow,black,brown]
color_names = ["red","green","blue","purple","orange","yellow","black","brown"]

RESOLUTION = (640,480)
calc_power_fun = None

def readline():
    line = sys.stdin.readline().rstrip()
    parts = line.split()
    return float(parts[0]), map(calc_power_fun, parts[1:])

def weightvalues(arr, weight):
    return map(lambda x: x*weight, arr)

def average_interval(interval_size):
    global interval_start
    global lasttime, lastvalues
    global nexttime, nextvalues
    channel_sums = [0.0]*num_channels
    lasttime = interval_start
    while nexttime < interval_start + interval_size:
        interval_weight = nexttime - lasttime
        channel_sums = map(sum, zip(channel_sums, weightvalues(nextvalues, interval_weight)))
        lasttime, lastvalues = nexttime, nextvalues
        nexttime, nextvalues = readline()
    interval_start += interval_size
    interval_weight = interval_start - lasttime
    channel_sums = map(sum, zip(channel_sums, weightvalues(nextvalues, interval_weight)))
    return map(lambda x: x/interval_size, channel_sums)

if len(sys.argv) != 2 and len(sys.argv) != 3 and len(sys.argv) != 5:
    print "USAGE: %s <window-time-frame> [<quality> [<voltage V> "\
            "<resistance mOhm>]]" % (sys.argv[0])
    print "Colors:"
    for name,num in zip(color_names, xrange(1, len(colors)+1)):
        print "\tchannel %d: %s" % (num, name)
    sys.exit(1)

time_resolution = float(sys.argv[1])
if len(sys.argv) == 3 or len(sys.argv) == 5:
    quality = int(sys.argv[2])
else:
    quality = 1

if len(sys.argv) == 5:
    calc_power_fun = lambda v: (float(v)*float(sys.argv[3]) / \
                               (float(sys.argv[4]) / 1000.0))
else:
    calc_power_fun = lambda v: float(v)

width = RESOLUTION[0]
window_width = width/quality
height = RESOLUTION[1]
height_used = int(0.8*height)
height_offset = (height-height_used)/2
time_per_sample= time_resolution/window_width

# read first value, set up variables
lasttime, lastvalues = readline()
nexttime, nextvalues = readline()
interval_start = lasttime
num_channels = len(lastvalues)

pygame.init()
screen = pygame.display.set_mode(RESOLUTION, pygame.DOUBLEBUF | pygame.HWSURFACE)

averages = []

avg_min = 10e1000
avg_max = -10e1000

list_index = 0
list_size = 0

old_time = pygame.time.get_ticks()
frames = 0

num_horizontal_ticks = 4
horizontal_ticks = []
font = pygame.font.Font(None, 17)

mouse_pos = (0, 0)

while 1:
    averages.append(average_interval(time_per_sample))
    averages = averages[-window_width:]
    if (len(averages) < window_width):
        if (len(averages)-1)%(window_width/num_horizontal_ticks) == 0:
            horizontal_ticks.append((len(averages), lasttime))
    else:
        horizontal_ticks = map(lambda x: (x[0]-1, x[1]), horizontal_ticks)
        if (horizontal_ticks[0][0] == 0):
            horizontal_ticks.pop(0)
            horizontal_ticks.append((window_width, lasttime))
    avg_min = min(avg_min, min(min(e) for e in averages))
    avg_max = max(avg_max, max(max(e) for e in averages))
    if (avg_min == avg_max):
        avg_max = avg_max*1.001
        avg_min = avg_min*0.999
    for event in pygame.event.get():
        if event.type == pygame.QUIT: sys.exit()
        elif event.type == pygame.MOUSEMOTION: mouse_pos = event.pos
    screen.fill(white)
    y1s = map(lambda x: height - int((x-avg_min)/(avg_max-avg_min)*height_used) - height_offset, averages[0])
    for i in range(1, len(averages)):
        y2s = map(lambda x: height - int((x-avg_min)/(avg_max-avg_min)*height_used) - height_offset, averages[i])
        for j in range(num_channels):
            pygame.draw.line(screen, colors[j], ((i-1)*quality, y1s[j]), (i*quality, y2s[j]))
        y1s = y2s
    pygame.draw.line(screen, black, (width, height_offset), (width-5, height_offset))
    pygame.draw.line(screen, black, (width, height/2), (width-5, height/2))
    pygame.draw.line(screen, black, (width, height-height_offset), (width-5, height-height_offset))
    text = font.render("%f" % avg_max, True, black)
    text_rect = text.get_rect(right=width-7,centery=height_offset)
    screen.blit(text, text_rect)
    text = font.render("%f" % ((avg_max+avg_min)/2), True, black)
    text_rect = text.get_rect(right=width-7,centery=height/2)
    screen.blit(text, text_rect)
    text = font.render("%f" % avg_min, True, black)
    text_rect = text.get_rect(right=width-7,centery=height-height_offset)
    screen.blit(text, text_rect)
    #mouse values
    (x, y) = mouse_pos
    my = (1.0-1.0*(y-height_offset)/height_used)*(avg_max-avg_min)+avg_min
    mx = (1.0*x/width)*(window_width*time_per_sample)+lasttime-(len(averages)*time_per_sample)
    text = font.render("(x=%f, y=%f)" % (mx, my), True, black)
    text_rect = text.get_rect(left=0, top=0)
    screen.blit(text, text_rect)
    for pos, val in horizontal_ticks:
        pygame.draw.line(screen, black, (quality*pos, height), (quality*pos, height-5))
        text = font.render("%s" % val, True, black)
        text_rect = text.get_rect(centerx=quality*pos, bottom=height-3)
        screen.blit(text, text_rect)
    pygame.display.flip()
    frames += 1
    new_time = pygame.time.get_ticks()
    if (new_time - old_time > 1000):
        print "FPS %f" % (1000.0*frames/(new_time - old_time))
        frames = 0
        old_time = new_time
