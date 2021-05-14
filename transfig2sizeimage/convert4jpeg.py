from PIL import Image
img = Image.open('../sample_image/3.jpeg')
new_img = img.resize((640, 480))
new_img.save("../sample_image/4.jpeg", "JPEG", optimize=True)

im = Image.open('../sample_image/4.jpeg')
pix = im.load()
f = open("../sample_image/2.raw", "w")
for j in range(480):
    for i in range(640):
        f.write(str(pix[i,j][2])+' '+str(pix[i,j][1])+' '+str(pix[i,j][0])+'\n')
f.close()
