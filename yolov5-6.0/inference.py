import torch
import cv2 as cv

# Use the local best.pt file in the current directory
model = torch.hub.load('.', 'custom', 
                        path='best.pt', 
                        source='local')
# BGR RGB
# Update the image path to a local test image
img = cv.imread("data/images/bus.jpg")  # Using a sample image from the repository
results = model(img)
results.save()
