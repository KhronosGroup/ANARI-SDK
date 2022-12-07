import skimage.metrics
import skimage.color
import skimage.util
import skimage.transform
import skimage.filters
import os
import warnings


def ssim(reference, candidate, threshold):
    result = skimage.metrics.structural_similarity(reference, candidate, channel_axis=2)
    return result > threshold, threshold, result

def psnr(reference, candidate, threshold):
    # Catch warning which is thrown if images are equal
    with warnings.catch_warnings():
        warnings.filterwarnings("ignore", message="divide by zero encountered in double_scalars")
        result = skimage.metrics.peak_signal_noise_ratio(reference, candidate)
    return result > threshold, threshold, result

def getThreshold(methods, thresholds, method, default):
    if thresholds != None:
        index = methods.index(method)
        if index < len(thresholds):
            return float(thresholds[index])
    return default

def evaluate_metrics(reference, candidate, methods, thresholds, custom_comparison_function):
    results = {}
    usedThresholds = {}
    passed = {}

    if "ssim" in methods:
        # Structural similarity index between two images
        # (humans are better at perceiving structural differences than subtle pixel changes).
        # it compares the images based on luminance Contast and structure.
        # ssim provides much better results when applied to small patches of the image than on its entirety. Typically an 11x11 window is used.
        # ssim provides scores from -1 (not similar) to 1 (same image).
        # ssim provides high scores one of the images is blured or the color space is shifted
        # further reading:
        # https://en.wikipedia.org/wiki/Structural_similarity
        threshold =  getThreshold(methods, thresholds, "ssim", 0.70)
        passed["ssim"], usedThresholds["ssim"], results["ssim"] = ssim(reference, candidate, threshold)
        
    if "psnr" in methods:
        # Mean Squared Error (MSE) is one of the most commonly used image quality measures, but receives strong criticism as
        # it doesn't reflect the way the human visual systems perceive images very well. An image pair with
        # high MSE might still look very similar to a human.
        # "mse": skimage.metrics.mean_squared_error(reference, candidate),
        # Peak Signal to Noise Ratio (PSNR) is based on MSE and brought to a logarithmic scale in the decibel unit
        threshold =  getThreshold(methods, thresholds, "psnr", 20.0)
        passed["psnr"], usedThresholds["psnr"], results["psnr"] = psnr(reference, candidate, threshold)

    if custom_comparison_function != None:
        passed["custom"], usedThresholds["custom"], results["custom"] = custom_comparison_function(reference, candidate)

    # Normalized Root MSE (NRMSE)
    # "nrmse": skimage.metrics.normalized_root_mse(reference, candidate),
    
    # Mean Absolute Error (MAE)
    # "mae": np.absolute(np.subtract(reference, candidate)).mean(),
    
    # Visual Information Fidelity Measure (VIF)
    # https://ieeexplore.ieee.org/abstract/document/1576816/
    # The higher the better. The reference image would yield a value of 1.0, values above 1.0
    # typically stem from higher contrast images, which are considerered better quality in this metric
    # "vif": vifp(reference, candidate),
    
    # Universal Quality Index (UQI)
    # "uqi": uqi(reference, candidate),
    return passed, usedThresholds, results 


def print_report(name, metrics_report):
    print(name)

    for name, value in metrics_report["metrics"].items():
        passed = ''
        if name in metrics_report['passed']:
            passed = f"[{'Passed' if metrics_report['passed'][name] else 'Failed'}]"
        print(f"{name: <8} {value :<12.6f} {passed}")

    print("")

def compare_images(reference, candidate):
    diff = skimage.util.compare_images(reference, candidate, method='diff')
    threshold = diff > 0.05

    return {
        "reference": reference,
        "candidate": candidate,
        "diff": skimage.util.img_as_ubyte(diff),
        "threshold": skimage.util.img_as_ubyte(threshold)
    }

def evaluate(reference, candidate, methods, thresholds, custom_compare_function):
    passed, usedThresholds, metrics = evaluate_metrics(reference, candidate, methods, thresholds, custom_compare_function)

    return {
        "metrics": metrics,
        "thresholds": usedThresholds,
        "passed": passed,
        "images": compare_images(reference, candidate),
    }

def normalize_images(reference, candidate):
    # Ensure images match in channel count
    if reference.shape[2] == 4:
        reference = skimage.color.rgba2rgb(reference)
        reference = skimage.util.img_as_ubyte(reference)
    if candidate.shape[2] == 4:
        candidate = skimage.color.rgba2rgb(candidate)
        candidate = skimage.util.img_as_ubyte(candidate)
    # Ensure images match in resolution
    if candidate.shape != reference.shape:
        candidate = skimage.transform.resize(candidate, (reference.shape[0], reference.shape[1]),
                    anti_aliasing=False)
        candidate = skimage.util.img_as_ubyte(candidate)
        print()
    return reference, candidate

def evaluate_scene(reference_path, candidate_path, methods, thresholds, custom_compare_function):
    reference_image = skimage.io.imread(reference_path)
    candidate_image = skimage.util.img_as_ubyte(skimage.io.imread(candidate_path))
    reference_image, candidate_image = normalize_images(reference_image, candidate_image)
    
    # Compute metrics and compare images
    return evaluate(reference_image, candidate_image, methods, thresholds, custom_compare_function)

def write_image(path, image, check_contrast = True):
    filepath, filename = os.path.split(path)
    if not os.path.exists(filepath):
        os.makedirs(filepath)
    skimage.io.imsave(path, image, check_contrast=check_contrast)
