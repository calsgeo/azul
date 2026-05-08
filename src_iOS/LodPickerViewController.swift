import UIKit

protocol LodPickerDelegate: AnyObject {
    func lodPickerDidSelect(_ lod: String)
}

class LodPickerViewController: UIViewController {

    weak var delegate: LodPickerDelegate?
    var availableLods: [String] = []
    var currentLod: String = ""

    let tableView = UITableView(frame: .zero, style: .plain)

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground

        tableView.delegate = self
        tableView.dataSource = self
        tableView.register(UITableViewCell.self, forCellReuseIdentifier: "cell")
        tableView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(tableView)

        NSLayoutConstraint.activate([
            tableView.topAnchor.constraint(equalTo: view.topAnchor),
            tableView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            tableView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            tableView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])

        if UIDevice.current.userInterfaceIdiom == .pad {
            preferredContentSize = CGSize(width: 300, height: min(CGFloat(availableLods.count + 1) * 44 + 44, 400))
        }

        navigationItem.rightBarButtonItem = UIBarButtonItem(barButtonSystemItem: .done, target: self, action: #selector(dismissSelf))
    }

    @objc func dismissSelf() {
        dismiss(animated: true)
    }
}

extension LodPickerViewController: UITableViewDataSource, UITableViewDelegate {
    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        availableLods.count + 1
    }

    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "cell", for: indexPath)

        if indexPath.row == 0 {
            cell.textLabel?.text = "Highest"
            cell.accessoryType = currentLod == "__highest__" ? .checkmark : .none
        } else {
            let lod = availableLods[indexPath.row - 1]
            cell.textLabel?.text = lod
            cell.accessoryType = currentLod == lod ? .checkmark : .none
        }

        cell.textLabel?.font = .systemFont(ofSize: UIFont.systemFontSize)
        cell.selectionStyle = .default

        return cell
    }

    func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)

        let lod: String
        if indexPath.row == 0 {
            lod = "__highest__"
        } else {
            lod = availableLods[indexPath.row - 1]
        }

        delegate?.lodPickerDidSelect(lod)
        dismiss(animated: true)
    }
}
