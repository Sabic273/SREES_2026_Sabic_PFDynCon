#pragma once
#include <gui/View.h>
#include <gui/Label.h>
#include <gui/TextEdit.h>
#include <gui/VerticalLayout.h>

// Staticki "Info" tab - objasnjava format fajlova koje plugin ucitava
// (GEN parametri generatora, PLOT/BUS poremecaji tereta). Nema nikakvu
// interakciju s korisnikom; tekst je formatiran koristeci TextEdit::setStyle()/
// setColor() na tacno izracunatim rasponima (header naslovi bold+akcent boja,
// primjeri kurziv), umjesto ASCII banner linija.
class ViewHelp : public gui::View
{
protected:
    gui::Label    _lblTitle;
    gui::TextEdit _te;
    gui::VerticalLayout _vl;

public:
    ViewHelp()
        : _lblTitle("Format fajlova")
        , _vl(2)
    {
        _lblTitle.setFont(gui::Font::ID::SystemLargestBold);
        _te.setAsReadOnly();

        const td::String bodyText("Plugin dinamicke parametre generatora i poremecaje tereta ucitava iz jednostavnih tekstualnih fajlova, umjesto da ih trazi kroz GUI obrasce. Ovakav pristup omogucava da se iste konfiguracije lako ponovo koriste, verzionisu i dijele izmedju razlicitih Matpower slucajeva, uz potpunu kontrolu nad svakim parametrom.\n\nDinamicki parametri generatora  (GEN fajl)\n\nOvaj fajl definise dinamicke parametre pojedinacnih generatora - inercijsku konstantu, reaktanse, te vremenske konstante regulatora napona i governora - koji se inace ne nalaze u Matpower formatu. Dostupan je i za male i za velike sisteme, putem dugmeta \"Ucitaj fajl\" u sekciji \"Parametri generatora\".\n\nSvaki generator se definise posebnim blokom koji pocinje redom GEN, nakon kojeg slijedi identifikator sabirnice na koju je generator prikljucen. Parametri se navode u obliku kljuc=vrijednost, jedan ili vise po redu, a blok se zavrsava redom END:\n\n    GEN 1\n    H=23.64 D=2 Xd=0.146 Xdp=0.0608 Xq=0.0969 Xqp=0.0969\n    Tdop=8.96 Tqop=0.31 Ka=200 Ta=0.02 Ke=1.0 Te=0.8\n    END\n\nPodrzani kljucevi su: H, D, Xd, Xdp, Xq, Xqp, Tdop, Tqop, Tr, Tgov, Ka, Ta, Ke, Te, Se, Kf, Tf, R i Rs. Nije potrebno navesti sve - parametri koji se izostave jednostavno ostaju na svojim standardnim vrijednostima, pa je moguce prilagoditi samo ono sto je stvarno poznato za konkretan generator.\n\nPrilikom ucitavanja, plugin provjerava da navedena sabirnica postoji u sistemu i da na njoj stvarno postoji generator; ako ne, ucitavanje se prekida uz jasnu poruku o gresci. Isto vrijedi za dupliran GEN blok za istu sabirnicu, kao i za vremenske konstante (H, Tdop, Tqop, Ta, Te, Tf, Tgov), koje moraju biti vece od nule jer se u modelu koriste kao djelilac.\n\n\nPoremecaji tereta i odabir prikaza  (BUS/PLOT fajl)\n\nZa sisteme sa vise od 50 sabirnica, poremecaji opterecenja (promjene P i Q tokom vremena) i izbor sabirnica koje se prikazuju na graficima definisu se u posebnom fajlu, umjesto kroz GUI - kod manjih sistema ovi podaci se i dalje unose direktno u prozoru za kreiranje modela.\n\nFajl moze opciono zapoceti jednim redom PLOT, koji navodi koje ce se sabirnice prikazati na svim graficima. Redovi koji slijede definisu poremecaje: BUS otvara blok za odredjenu sabirnicu, nakon cega slijedi jedan ili vise redova sa vremenom i novim vrijednostima opterecenja, a END zatvara blok:\n\n    PLOT 1 6 11 15\n\n    BUS 15\n    0.5  1.10  0.45\n    6.0  0.90  0.30\n    END\n\nPLOT prihvata pojedinacne brojeve sabirnica, opsege (npr. 5-9) i kombinacije odvojene zarezom; ako se izostavi, na graficima se prikazuju sve sabirnice. Unutar svakog BUS bloka, svaki red predstavlja jednu vremensku tacku u obliku \"vrijeme[s] Pd[pu] Qd[pu]\", a tacke se automatski sortiraju po vremenu bez obzira na redosljed unosa. Pd i Qd su izrazeni u per-unit sistemu (MW/MVAr podijeljeni sa baseMVA), na isti nacin kao i originalno opterecenje iz Matpower fajla.\n\nI ovdje se provjerava postojanje navedenih sabirnica, kao i da svaki blok sadrzi bar jednu vremensku tacku i ispravno zavrsava sa END. Redovi koji pocinju znakom # se u oba formata tretiraju kao komentar i preskacu se.\n");
        _te.setText(bodyText);

        // Naslovi sekcija: bold + plava akcentna boja.
        _te.setStyle(gui::Range(315, 44), gui::Font::Style::Bold);
        _te.setColor(gui::Range(315, 44), td::ColorID::DarkBlue);
        _te.setStyle(gui::Range(1697, 53), gui::Font::Style::Bold);
        _te.setColor(gui::Range(1697, 53), td::ColorID::DarkBlue);

        // Primjeri fajlova: kurziv, da se vizuelno odvoje od opisnog teksta.
        _te.setStyle(gui::Range(909, 129), gui::Font::Style::Italic);
        _te.setStyle(gui::Range(2324, 80), gui::Font::Style::Italic);

        _vl << _lblTitle << _te;
        setLayout(&_vl);
    }
};